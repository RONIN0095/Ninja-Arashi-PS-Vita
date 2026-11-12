/*
 * Hides the game's on-screen touch controls, and a few things of the menu.
 *
 * The Vita's buttons press those controls (na_input.c), so they need not be
 * seen. The visible parts are child objects of the controls (the arrow and
 * icon images; see the level scenes); the control object itself keeps its
 * own graphic, the touch area, and its event handlers. On the main thread
 * between frames, those child objects are switched off with
 * SetActive(false). (IL2CPP stripped GetComponentsInChildren(Type, bool) and
 * CanvasRenderer.SetAlpha from this build, so the objects are switched off
 * rather than made transparent.) The main menu's "Remove Ads" button and
 * the "rate us" pop-up go the same way: there is no store on the Vita.
 * See na_hud_tick for how the objects are found.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "na_hud.h"
#include "na_modules.h"
#include "utils/logger.h"

#include <psp2/kernel/processmgr.h>

typedef void *(*domain_get_t)(void);
typedef void *(*assembly_open_t)(void *domain, const char *name);
typedef void *(*assembly_image_t)(void *assembly);
typedef void *(*class_from_name_t)(void *image, const char *ns, const char *name);
typedef void *(*method_from_name_t)(void *klass, const char *name, int argc);
typedef void *(*runtime_invoke_t)(void *method, void *obj, void **params, void **exc);
typedef void *(*string_new_t)(const char *s);

static runtime_invoke_t invoke;
static string_new_t string_new;
static void *m_find, *m_set_active, *m_set_shadows, *m_set_shadow_distance;
static void *m_get_transform, *m_tr_find, *m_get_game_object, *m_active_in_hierarchy;
static void *m_get_fps, *m_set_fps;
static int state = 0; // 0 not set up, 1 ready, -1 failed
static volatile int in_level = 0; // the level's touch controls are up

int na_hud_in_level(void) {
    return in_level;
}

// The privacy pop-up at the start (Canvas/GDPR/GDPR Popup) is up
static volatile int gdpr_up = 0;
int na_hud_gdpr_up(void) {
    return gdpr_up;
}

/*
 * What is hidden: children of two root objects, by their path under it.
 * "Canvas/PlayButton" holds the level's touch controls (the Vita's buttons
 * press them, na_input.c); "CanvasMain" is the main menu, with the "Remove
 * Ads" button and the "rate us" pop-up and button (no store on the Vita).
 */
typedef struct {
    const char *root;
    const char *const *children;
    int n;
} hud_root;

static const char *const play_children[] = {
    "Left/image", "Right/image", "Skill/Up/Image", "Skill/Fi/Image", "Skill/Dash/Image", "Skill/Check/Check image",
    "Trans/Image",
};
static const char *const menu_children[] = { "LevelChoiseCanVas/Remove Ads", "RateUs", "Setting/main/Rateus" };
#define ROOT_PLAY 0
#define ROOT_MENU 1
#define N_ROOTS 2
#define MAX_CHILDREN 8
static const hud_root roots[N_ROOTS] = {
    { "Canvas/PlayButton", play_children, sizeof(play_children) / sizeof(play_children[0]) },
    { "CanvasMain", menu_children, sizeof(menu_children) / sizeof(menu_children[0]) },
};

static void *sym(const char *name) {
    return (void *)na_module_symbol(NA_LIB_IL2CPP, name);
}

static uint32_t (*gchandle_new)(void *obj, int pinned);
static void (*gchandle_free)(uint32_t handle);

static int setup(void) {
    domain_get_t domain_get = sym("il2cpp_domain_get");
    assembly_open_t assembly_open = sym("il2cpp_domain_assembly_open");
    assembly_image_t assembly_image = sym("il2cpp_assembly_get_image");
    class_from_name_t class_from_name = sym("il2cpp_class_from_name");
    method_from_name_t method_from_name = sym("il2cpp_class_get_method_from_name");
    invoke = sym("il2cpp_runtime_invoke");
    string_new = sym("il2cpp_string_new");
    gchandle_new = sym("il2cpp_gchandle_new");
    gchandle_free = sym("il2cpp_gchandle_free");
    if (!domain_get || !assembly_open || !assembly_image || !class_from_name || !method_from_name || !invoke ||
        !string_new || !gchandle_new || !gchandle_free) {
        l_warn("[HUD] IL2CPP API missing");
        return -1;
    }
    void *core = assembly_open(domain_get(), "UnityEngine.CoreModule");
    void *img = core ? assembly_image(core) : NULL;
    void *go = img ? class_from_name(img, "UnityEngine", "GameObject") : NULL;
    void *tr = img ? class_from_name(img, "UnityEngine", "Transform") : NULL;
    void *comp = img ? class_from_name(img, "UnityEngine", "Component") : NULL;
    if (!go) {
        l_warn("[HUD] GameObject class not found");
        return -1;
    }
    // shadows: a 2D game on a small screen; the shadow pass cost GPU time
    void *qs = class_from_name(img, "UnityEngine", "QualitySettings");
    if (qs) {
        m_set_shadows = method_from_name(qs, "set_shadows", 1);
        m_set_shadow_distance = method_from_name(qs, "set_shadowDistance", 1);
    }
    void *app = class_from_name(img, "UnityEngine", "Application");
    if (app) {
        m_get_fps = method_from_name(app, "get_targetFrameRate", 0);
        m_set_fps = method_from_name(app, "set_targetFrameRate", 1);
    }
    m_find = method_from_name(go, "Find", 1);
    m_set_active = method_from_name(go, "SetActive", 1);
    m_get_transform = method_from_name(go, "get_transform", 0);
    m_active_in_hierarchy = method_from_name(go, "get_activeInHierarchy", 0);
    m_tr_find = tr ? method_from_name(tr, "Find", 1) : NULL;
    m_get_game_object = comp ? method_from_name(comp, "get_gameObject", 0) : NULL;
    l_info("[HUD] Find %p, SetActive %p, get_transform %p, Transform.Find %p, get_gameObject %p, activeInHierarchy %p, "
           "targetFrameRate %p/%p, shadows %p/%p", m_find, m_set_active, m_get_transform, m_tr_find, m_get_game_object,
           m_active_in_hierarchy, m_get_fps, m_set_fps, m_set_shadows, m_set_shadow_distance);
    return m_find && m_set_active ? 1 : -1;
}

static void *call(void *method, void *obj, void **params) {
    void *exc = NULL;
    void *r = invoke(method, obj, params, &exc);
    return exc ? NULL : r;
}

/*
 * GameObject.Find walks the whole scene by name: about 2 ms a call in a
 * level, and thirteen calls a frame took most of a frame's time. Now:
 *
 * - An object that was found is held (with a GC handle, so its managed
 *   object stays) and used directly. When its scene goes, its native
 *   pointer (m_CachedPtr, the first field) is cleared and it is let go.
 * - The two roots are looked up with GameObject.Find, but only while they
 *   are not held, and never the menu's root during a level.
 * - Their children are looked up from the root with Transform.Find, which
 *   only walks the root's own few children, and also finds objects that
 *   are switched off: they are held before the game ever shows them, and
 *   switched off again every frame.
 * - The path strings are made once and kept.
 *
 * Lookups run every frame for two seconds after a slow frame (a scene was
 * loaded) and every 10th frame otherwise.
 */
typedef struct {
    void *obj;
    uint32_t handle;
} held_obj;

static held_obj root_obj[N_ROOTS], child_obj[N_ROOTS][MAX_CHILDREN];
static void *root_str[N_ROOTS], *child_str[N_ROOTS][MAX_CHILDREN], *child_full_str[N_ROOTS][MAX_CHILDREN], *gdpr_str;

static void *kept_string(const char *s) {
    void *str = string_new(s);
    if (str) gchandle_new(str, 0);
    return str;
}

static int alive(void *obj) {
    return obj && *(void **)((char *)obj + 8) != NULL; // UnityEngine.Object.m_CachedPtr
}

static void hold(held_obj *h, void *obj) {
    h->obj = obj;
    h->handle = gchandle_new(obj, 0);
}

// 1 while the object is there; lets it go when its scene is gone
static int still(held_obj *h) {
    if (!h->obj) return 0;
    if (alive(h->obj)) return 1;
    gchandle_free(h->handle);
    h->obj = NULL;
    return 0;
}

volatile uint32_t na_hud_us = 0; // time spent here, for the frame log (main.c)

void na_hud_tick(int frame) {
    if (state < 0 || frame < 30) return;
    uint64_t t_start = sceKernelGetProcessTimeWide();
    if (state == 0) {
        state = setup();
        if (state < 0) return;
        for (int r = 0; r < N_ROOTS; r++) {
            root_str[r] = kept_string(roots[r].root);
            for (int c = 0; c < roots[r].n; c++) {
                char full[160];
                snprintf(full, sizeof(full), "%s/%s", roots[r].root, roots[r].children[c]);
                child_str[r][c] = kept_string(roots[r].children[c]);
                child_full_str[r][c] = kept_string(full);
            }
        }
        gdpr_str = kept_string("Canvas/GDPR/GDPR Popup");
    }
    if (frame % 300 == 30) {
        // shadows off (again after scene loads)
        int none = 0;
        float zero = 0.0f;
        void *a[1];
        if (m_set_shadows) {
            a[0] = &none;
            call(m_set_shadows, NULL, a);
        }
        if (m_set_shadow_distance) {
            a[0] = &zero;
            call(m_set_shadow_distance, NULL, a);
        }
        // Frame rate: Unity holds a phone game at 30 fps unless the game
        // asks for more (the menu ran at 29.5); ask for the screen's 60
        if (m_get_fps && m_set_fps) {
            void *boxed = call(m_get_fps, NULL, NULL);
            int fps = boxed ? *(int *)((char *)boxed + 8) : 0;
            static int last_fps = -12345;
            if (fps != last_fps) {
                last_fps = fps;
                l_info("[HUD] Application.targetFrameRate is %d", fps);
            }
            if (fps != 60) {
                int want = 60;
                a[0] = &want;
                call(m_set_fps, NULL, a);
            }
        }
    }
    static uint64_t last_tick = 0;
    static int fast_until = 150;
    if (last_tick && t_start - last_tick > 150000) fast_until = frame + 120;
    last_tick = t_start;
    int scan = frame < fast_until || frame % 10 == 0;

    int have_play = still(&root_obj[ROOT_PLAY]), have_menu = still(&root_obj[ROOT_MENU]);
    int hidden = 0;
    if (scan) {
        if (!have_play) {
            void *a[1] = { root_str[ROOT_PLAY] };
            void *obj = call(m_find, NULL, a);
            if (obj) {
                hold(&root_obj[ROOT_PLAY], obj);
                have_play = 1;
                fast_until = frame + 120;
            }
        }
        if (!have_play && !have_menu) {
            void *a[1] = { root_str[ROOT_MENU] };
            void *obj = call(m_find, NULL, a);
            if (obj) {
                hold(&root_obj[ROOT_MENU], obj);
                have_menu = 1;
            }
        }
        if (!have_play && frame < 3000) { // the privacy pop-up only comes at the start
            void *a[1] = { gdpr_str };
            gdpr_up = call(m_find, NULL, a) != NULL;
        } else {
            gdpr_up = 0;
        }
    }
    for (int r = 0; r < N_ROOTS; r++) {
        if (!still(&root_obj[r])) {
            for (int c = 0; c < roots[r].n; c++) still(&child_obj[r][c]);
            continue;
        }
        void *tr = NULL;
        for (int c = 0; c < roots[r].n; c++) {
            held_obj *h = &child_obj[r][c];
            if (!still(h) && scan) {
                void *obj = NULL;
                if (m_get_transform && m_tr_find && m_get_game_object) {
                    if (!tr) tr = call(m_get_transform, root_obj[r].obj, NULL);
                    void *a[1] = { child_str[r][c] };
                    void *child = tr ? call(m_tr_find, tr, a) : NULL;
                    obj = child ? call(m_get_game_object, child, NULL) : NULL;
                } else if (frame < fast_until || frame % 30 == 0) {
                    // without Transform.Find: by its whole path (only finds it once it is switched on)
                    void *a[1] = { child_full_str[r][c] };
                    obj = call(m_find, NULL, a);
                }
                if (obj) {
                    hold(h, obj);
                    hidden++;
                }
            }
            if (h->obj) {
                uint8_t off = 0;
                void *a[1] = { &off };
                call(m_set_active, h->obj, a);
            }
        }
    }
    // In a level: the controls' root is there and switched on (the game
    // switches it off for its pause and end screens, where the cursor is used)
    static int play_active = 0;
    if (!have_play) play_active = 0;
    else if (!m_active_in_hierarchy) play_active = 1;
    else if (scan || frame % 4 == 0) {
        void *boxed = call(m_active_in_hierarchy, root_obj[ROOT_PLAY].obj, NULL);
        play_active = boxed ? *(uint8_t *)((char *)boxed + 8) != 0 : 0;
    }
    if (play_active != in_level) fast_until = frame + 120;
    in_level = play_active;

    static int logged = 0;
    if (hidden && logged < 8) {
        logged++;
        l_info("[HUD] %d objects found and hidden", hidden);
    }
    na_hud_us += (uint32_t)(sceKernelGetProcessTimeWide() - t_start);
}
