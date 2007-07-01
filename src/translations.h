#ifndef TRANSLATIONS_H__
#define TRANSLATIONS_H__

/* Note: it could be a singleton class but writing sigletons is stupid. */
typedef struct LanguagesList {
    const char* m_langName;
    struct LanguagesList* m_next;
    /* this is for internal use, don't touch this */
    int m_id; /* 0..n */
} LanguagesList;

LanguagesList* Translations_FromData(const char* data, size_t data_len);
LanguagesList* Translations_GetLanguages();
bool Translations_SetCurrentLanguage(const char* lang);
const char* Translatations_GetTranslation(const char* data);
void Translations_FreeData();
#endif
