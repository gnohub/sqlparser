#ifndef ORACLE_CONSTRUCTORGATES_CASES_H
#define ORACLE_CONSTRUCTORGATES_CASES_H
static const char *const constructorgates_cases[] = {
    "t", "_t", "T9", "a.b", "a.b.c", "a.b.c.d", "a.b.c.d.e",
    "", " ", "\t\r\n", ".", "..", ".t", "t.", "a..b", "a...b", "a.b.",
    " t ", " a.b ", "a .b", "a. b", "a . b", "a . b . c", "a\t.\r\nb",
    "a b", "a b.c", "a. b c", "a . . b", " a\v.\fb ",
    "1t", "a.2b", "$a", "a$b", "#a", "a#b", "a_b.c_2", "a-b", "a+b",
    "\"a.b\"", "\"a\".\"b\"", "\"a\"\"b\".c", "\"\"", "\"a\".\"\"", "\"broken",
    "'a.b'", "q'[a.b]'", "$$a.b$$", "`a.b`", "[a.b]",
    "a/* . */.b", "a. /* . */ b", "/*head*/a", "a/*tail*/", "a-- .\n.b", "a/*broken",
    "t@link", "a.b@remote", "a.b.c@remote", "t@\"a.b\"", "t @ link", "@link", "t@",
    "t@a.b", "t@@link", "t@\"\"", "t@link extra", "a.b.c.d@remote",
    "\xce\xa9", "a.\xe4\xb8\xad", "a\240.b", "\335.t", "a.\377",
    "values", "valuesx", "myvalues", "_values", "into", "select", "\"values\"", "t values"
};
static const char *const constructorgates_locales[] = {
    "C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US",
    "English_United States.1252", "Turkish_Turkey.1254"
};
#endif
