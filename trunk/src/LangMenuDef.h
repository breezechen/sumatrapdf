/* Generated by script gen_c_from_.py
    DO NOT EDIT MANUALLY */

#ifndef LANG_MENU_DEF_H__
#define LANG_MENU_DEF_H__

#define LANGS_COUNT 60

typedef struct LangDef {
    const char *_langName;
    const char *_langMenuTitle;
    LANGID _langId;
} LangDef;

extern LangDef g_langs[LANGS_COUNT];

#endif
