#ifndef MODS_H
#define MODS_H
/* Gestor de mods activos.

   Lee modlist.txt / mods.txt / currentmod.txt (primero que exista) con lineas:
     mimod          -> activo (sin flag = 1)
     mimod 1        -> activo
     mimod 0        -> inactivo
     mimod:1        -> activo (soporta ':')

   Luego escanea mods/ y auto-añade cualquier carpeta no listada con flag 1.
   Guarda el .txt actualizado al final (nuevas entradas quedan registradas).

   Expone: ActiveMods.mods[i].{name, active, in_list}
           ActiveMods.count

   Uso:
     mods_load();
     for (i = 0; i < ActiveMods.count; i++)
         if (ActiveMods.mods[i].active)
             // usar mods/<name>/...

   Compatibilidad: si no hay modlist.txt pero hay currentmod.txt, se migra
   automaticamente (el mod de currentmod.txt se pone en la lista con 1). */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODS_MAX      64
#define MOD_NAME_MAX  48

typedef struct {
    char name[MOD_NAME_MAX];
    int  active;    /* 1 = cargar, 0 = ignorar */
    int  in_list;   /* 1 = ya estaba en el .txt, 0 = auto-añadido */
} ModEntry;

typedef struct {
    ModEntry mods[MODS_MAX];
    int      count;
    char     listPath[128];
} ModList;

static ModList ActiveMods;

static void mods_trim(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\n' || s[n-1] == '\r'))
        s[--n] = 0;
}

static int mods_find(const char *name) {
    int i;
    for (i = 0; i < ActiveMods.count; i++)
        if (strcmp(ActiveMods.mods[i].name, name) == 0) return i;
    return -1;
}

static int mods_add(const char *name, int active, int in_list) {
    ModEntry *m;
    if (ActiveMods.count >= MODS_MAX) return -1;
    if (mods_find(name) >= 0) return -1;
    m = &ActiveMods.mods[ActiveMods.count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->name, name, MOD_NAME_MAX - 1);
    m->active  = active;
    m->in_list = in_list;
    return 0;
}

static int mods_load_list(const char *path) {
    FILE *f = fopen(path, "rb");
    char line[128];
    int found_any = 0;

    if (!f) return -1;
    strncpy(ActiveMods.listPath, path, sizeof(ActiveMods.listPath) - 1);
    while (fgets(line, sizeof(line), f)) {
        char *name = line;
        char *val = NULL;
        char *p;
        int active = 1;

        while (*name == ' ' || *name == '\t') name++;
        if (!*name || *name == '#' || *name == '\n' || *name == '\r') continue;

        p = name;
        while (*p && *p != ' ' && *p != '\t' && *p != ':' && *p != '\n' && *p != '\r') p++;
        if (*p) {
            *p++ = 0;
            while (*p == ' ' || *p == '\t' || *p == ':') p++;
            val = p;
            mods_trim(val);
            if (*val == '0') active = 0;
            else if (*val == '1') active = 1;
            else active = 1;
        }
        mods_trim(name);
        if (name[0]) {
            mods_add(name, active, 1);
            found_any = 1;
        }
    }
    fclose(f);
    return found_any ? 0 : -2;
}

static int mods_scan_dir(void) {
    SceUID dh = sceIoDopen("mods");
    SceIoDirent entry;
    int added = 0;

    if (dh < 0) return -1;
    while (sceIoDread(dh, &entry) > 0) {
        if (!FIO_S_ISDIR(entry.d_stat.st_mode)) continue;
        if (entry.d_name[0] == '.') continue;
        if (mods_find(entry.d_name) >= 0) continue;
        if (mods_add(entry.d_name, 1, 0) == 0) added++;
    }
    sceIoDclose(dh);
    return added;
}

static int mods_save(void) {
    FILE *f;
    int i;
    if (!ActiveMods.listPath[0])
        strncpy(ActiveMods.listPath, "modlist.txt", sizeof(ActiveMods.listPath) - 1);
    f = fopen(ActiveMods.listPath, "wb");
    if (!f) return -1;
    for (i = 0; i < ActiveMods.count; i++)
        fprintf(f, "%s %d\n", ActiveMods.mods[i].name, ActiveMods.mods[i].active ? 1 : 0);
    fclose(f);
    return 0;
}

static int mods_load(void) {
    static const char *CANDIDATES[] = { "modlist.txt", "mods.txt", "currentmod.txt" };
    int i, loaded = 0, added;

    memset(&ActiveMods, 0, sizeof(ActiveMods));

    for (i = 0; i < 3 && !loaded; i++) {
        if (mods_load_list(CANDIDATES[i]) == 0) loaded = 1;
    }

    added = mods_scan_dir();
    if (added > 0) mods_save();

    printf("[mods] %d entradas (%d auto-añadidas). Lista: %s\n",
           ActiveMods.count, added > 0 ? added : 0,
           ActiveMods.listPath[0] ? ActiveMods.listPath : "(ninguna)");
    for (i = 0; i < ActiveMods.count; i++)
        printf("[mods]   %s %s\n", ActiveMods.mods[i].name,
               ActiveMods.mods[i].active ? "ON" : "off");

    return ActiveMods.count > 0 ? 0 : -1;
}

/* Devuelve el primer mod activo, o "" si no hay. */
static void mods_first_active(char *out, int len) {
    int i;
    out[0] = 0;
    for (i = 0; i < ActiveMods.count; i++)
        if (ActiveMods.mods[i].active) {
            strncpy(out, ActiveMods.mods[i].name, len - 1);
            out[len - 1] = 0;
            return;
        }
}

#endif