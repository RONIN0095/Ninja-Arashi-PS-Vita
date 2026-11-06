/*
 * Hides the game's on-screen touch controls.
 *
 * The Vita's buttons press those controls (na_input.c), so they need not be
 * seen. The visible parts are child graphics of the control objects (the
 * arrow and icon images, cooldown overlays); the control object itself keeps
 * its own graphic, which is the touch area. Every second, on the main thread
 * between frames, the controls are looked up through IL2CPP and the
 * CanvasRenderers of their children get alpha 0 (with transparent-mesh
 * culling off, so they stay in the canvas like before).
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
typedef void *(*class_get_type_t)(void *klass);
typedef void *(*type_get_object_t)(void *type);
typedef uint32_t (*array_length_t)(void *array);
typedef void *(*class_get_methods_t)(void *klass, void **iter);
typedef const char *(*method_get_name_t)(void *method);
typedef uint32_t (*method_param_count_t)(void *method);
typedef uint8_t (*method_is_generic_t)(void *method);

// A non-generic method by name and parameter count (GetComponentsInChildren
// and GetComponent also have generic overloads with as many parameters)
static void *plain_method(void *klass, const char *name, uint32_t argc) {
    class_get_methods_t get_methods = (class_get_methods_t)na_module_symbol(NA_LIB_IL2CPP, "il2cpp_class_get_methods");
    method_get_name_t get_name = (method_get_name_t)na_module_symbol(NA_LIB_IL2CPP, "il2cpp_method_get_name");
    method_param_count_t param_count =
        (method_param_count_t)na_module_symbol(NA_LIB_IL2CPP, "il2cpp_method_get_param_count");
    method_is_generic_t is_generic = (method_is_generic_t)na_module_symbol(NA_LIB_IL2CPP, "il2cpp_method_is_generic");
    if (!get_methods || !get_name || !param_count || !is_generic) return NULL;
    void *iter = NULL, *m;
    while ((m = get_methods(klass, &iter)) != NULL)
        if (strcmp(get_name(m), name) == 0 && param_count(m) == argc && !is_generic(m)) return m;
    return NULL;
}

static runtime_invoke_t invoke;
static string_new_t string_new;
static array_length_t array_length;
static void *m_find, *m_get_children, *m_set_alpha, *m_set_cull;
static void *cr_type;
static int state = 0; // 0 not set up, 1 ready, -1 failed

static const char *const controls[] = {
    "Canvas/PlayButton/Left",       "Canvas/PlayButton/Right",      "Canvas/PlayButton/Skill/Up",
    "Canvas/PlayButton/Skill/Fi",   "Canvas/PlayButton/Skill/Dash", "Canvas/PlayButton/Skill/Check",
    "Canvas/PlayButton/Trans",
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
    class_get_type_t class_get_type = sym("il2cpp_class_get_type");
    type_get_object_t type_get_object = sym("il2cpp_type_get_object");
    invoke = sym("il2cpp_runtime_invoke");
    string_new = sym("il2cpp_string_new");
    array_length = sym("il2cpp_array_length");
    if (!domain_get || !assembly_open || !assembly_image || !class_from_name || !method_from_name ||
        !class_get_type || !type_get_object || !invoke || !string_new || !array_length) {
        l_warn("[HUD] IL2CPP API missing");
        return -1;
    }
    void *domain = domain_get();
    void *core = assembly_open(domain, "UnityEngine.CoreModule");
    void *ui = assembly_open(domain, "UnityEngine.UIModule");
    if (!core || !ui) {
        l_warn("[HUD] assemblies not found (core %p, ui %p)", core, ui);
        return -1;
    }
    void *go = class_from_name(assembly_image(core), "UnityEngine", "GameObject");
    void *cr = class_from_name(assembly_image(ui), "UnityEngine", "CanvasRenderer");
    if (!go || !cr) {
        l_warn("[HUD] classes not found (GameObject %p, CanvasRenderer %p)", go, cr);
        return -1;
    }
    m_find = method_from_name(go, "Find", 1);
    m_get_children = plain_method(go, "GetComponentsInChildren", 2);
    m_set_alpha = method_from_name(cr, "SetAlpha", 1);
    m_set_cull = method_from_name(cr, "set_cullTransparentMesh", 1);
    cr_type = type_get_object(class_get_type(cr));
    l_info("[HUD] Find %p, GetComponentsInChildren %p, SetAlpha %p, cullTransparentMesh %p", m_find, m_get_children,
           m_set_alpha, m_set_cull);
    if (!m_find || !m_get_children || !m_set_alpha || !cr_type) return -1;
    return 1;
}

static void *call(void *method, void *obj, void **params) {
    void *exc = NULL;
    void *r = invoke(method, obj, params, &exc);
    return exc ? NULL : r;
}

static int hide(const char *path) {
    void *args[2];
    args[0] = string_new(path);
    void *go = call(m_find, NULL, args);
    if (!go) return 0;
    uint8_t include_inactive = 1;
    args[0] = cr_type;
    args[1] = &include_inactive;
    void *arr = call(m_get_children, go, args);
    if (!arr) return 0;
    uint32_t n = array_length(arr);
    void **items = (void **)((uint8_t *)arr + 16); // 32-bit Il2CppArray header
    int hidden = 0;
    for (uint32_t i = 0; i < n; i++) {
        void *c = items[i];
        // the first is the control's own renderer (all of them have one, see the
        // level scenes): its graphic is the touch area
        if (!c || i == 0) continue;
        uint8_t off = 0;
        if (m_set_cull) {
            args[0] = &off;
            call(m_set_cull, c, args);
        }
        float zero = 0.0f;
        args[0] = &zero;
        call(m_set_alpha, c, args);
        hidden++;
    }
    return hidden;
}

void na_hud_tick(int frame) {
    if (state < 0 || frame < 60 || frame % 30 != 0) return;
    if (state == 0) {
        state = setup();
        if (state < 0) return;
    }
    static int logged = 0;
    int total = 0;
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) total += hide(controls[i]);
    if (total && logged < 4) {
        logged++;
        l_info("[HUD] touch controls hidden (%d graphics)", total);
    }
}
