#include "base_util.h"
#include "translations.h"
#include "str_util.h"

/* For simplicity, maximum number of languages is defined upfront. If you need
   more, change the value and recompile */
#define MAX_LANGS 5

static LanguagesList* g_langList;

/* 'data'/'data_len' is a text describing all texts we translate.
   It builds data structures need for quick lookup of translations
   as well as a list of available languages.
   It returns a list of available languages.
   The list is not valid after a call to Translations_FreeData.
   The function must be called before any other function in this module.
   It can be called multiple times. This is to make debugging of translations
   easier by allowing re-loading translation file at runtime. 
   */
LanguagesList* Translations_FromData(const char* data, size_t data_len)
{

    return NULL;
}

LanguagesList* Translations_GetLanguages()
{

    return NULL;
}

bool Translations_SetCurrentLanguage(const char* lang)
{

    return false;
}

const char* Translatations_GetTranslation(const char* data)
{

    return NULL;
}

void Translations_FreeData()
{

}

#undef MAX_LANGS
