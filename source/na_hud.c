/*
 * Hides the game's on-screen touch controls.
 *
 * The Vita's buttons press those controls (na_input.c), so they need not be
 * seen. The visible parts are child objects of the controls (the arrow and
 * icon images; see the level scenes); the control object itself keeps its
 * own graphic, the touch area, and its event handlers. Every second, on the
 * main thread between frames, those child objects are looked up with
 * GameObject.Find and switched off with SetActive(false). (IL2CPP stripped
 * GetComponentsInChildren(Type, bool) and CanvasRenderer.SetAlpha from this
 * build, so the objects are switched off rather than made transparent.)
 * Find only returns active objects: controls the game turns on later are
 * caught on a later pass.
 */

#include <stdint.h>
#include <string.h>

#include "na_hud.h"
#include "na_modules.h"
#include "utils/logger.h"

typedef void *(*domain_get_t)(void);
typedef void *(*assembly_open_t)(void *domain, const char *name);
typedef void *(*assembly_image_t)(void *assembly);
typedef void *(*class_from_name_t)(void *image, const char *ns, const char *name);
typedef void *(*method_from_name_t)(void *klass, const char *name, int argc);
typedef void *(*runtime_invoke_t)(void *method, void *obj, void **params, void **exc);
typedef void *(*string_new_t)(const char *s);

static runtime_invoke_t invoke;
static string_new_t string_new;
static void *m_find, *m_set_active;
static int state = 0; // 0 not set up, 1 ready, -1 failed

static const char *const parts[] = {
    "Canvas/PlayButton/Left/image",       "Canvas/PlayButton/Right/image",
    "Canvas/PlayButton/Skill/Up/Image",   "Canvas/PlayButton/Skill/Fi/Image",
    "Canvas/PlayButton/Skill/Dash/Image", "Canvas/PlayButton/Skill/Check/Check image",
    "Canvas/PlayButton/Trans/Image",
};

static void *sym(const char *name) {
    return (void *)na_module_symbol(NA_LIB_IL2CPP, name);
}

static int setup(void) {
    domain_get_t domain_get = sym("il2cpp_domain_get");
    assembly_open_t assembly_open = sym("il2cpp_domain_assembly_open");
    assembly_image_t assembly_image = sym("il2cpp_assembly_get_image");
    class_from_name_t class_from_name = sym("il2cpp_class_from_name");
    method_from_name_t method_from_name = sym("il2cpp_class_get_method_from_name");
    invoke = sym("il2cpp_runtime_invoke");
    string_new = sym("il2cpp_string_new");
    if (!domain_get || !assembly_open || !assembly_image || !class_from_name || !method_from_name || !invoke ||
        !string_new) {
        l_warn("[HUD] IL2CPP API missing");
        return -1;
    }
    void *core = assembly_open(domain_get(), "UnityEngine.CoreModule");
    void *go = core ? class_from_name(assembly_image(core), "UnityEngine", "GameObject") : NULL;
    if (!go) {
        l_warn("[HUD] GameObject class not found");
        return -1;
    }
    m_find = method_from_name(go, "Find", 1);
    m_set_active = method_from_name(go, "SetActive", 1);
    l_info("[HUD] Find %p, SetActive %p", m_find, m_set_active);
    return m_find && m_set_active ? 1 : -1;
}

static void *call(void *method, void *obj, void **params) {
    void *exc = NULL;
    void *r = invoke(method, obj, params, &exc);
    return exc ? NULL : r;
}

void na_hud_tick(int frame) {
    if (state < 0 || frame < 60 || frame % 30 != 0) return;
    if (state == 0) {
        state = setup();
        if (state < 0) return;
    }
    static int logged = 0;
    int hidden = 0;
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        void *args[1] = { string_new(parts[i]) };
        void *obj = call(m_find, NULL, args);
        if (!obj) continue;
        uint8_t off = 0;
        args[0] = &off;
        call(m_set_active, obj, args);
        hidden++;
    }
    if (hidden && logged < 8) {
        logged++;
        l_info("[HUD] %d touch control images hidden", hidden);
    }
}
