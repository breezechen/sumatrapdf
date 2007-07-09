#include "base_util.h"
#include "translations.h"
#include "str_util.h"

/* Note: it could be a singleton class but writing sigletons is stupid. */
typedef struct Language {
    const char* m_langName;
    int m_id; /* 0..n */
} Language;

static Language* g_langs;

/* 'data'/'data_len' is a text describing all texts we translate.
   It builds data structures need for quick lookup of translations
   as well as a list of available languages.
   It returns a list of available languages.
   The list is not valid after a call to Translations_FreeData.
   The function must be called before any other function in this module.
   It can be called multiple times. This is to make debugging of translations
   easier by allowing re-loading translation file at runtime. 
   */
bool Translations_FromData(const char* langs, const char* data, size_t data_len)
{

    return false;
}

bool Translations_SetCurrentLanguage(const char* lang)
{

    return false;
}

const char* Translatations_GetTranslation(const char* txt)
{

    return txt;
}

void Translations_FreeData()
{

}

