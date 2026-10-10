#ifndef ORACLE_VALUEFACTS_CASES_H
#define ORACLE_VALUEFACTS_CASES_H

static const char *const valuefacts_cases[] = {
    "''", "'plain'", "'\xce\xa9\xe4\xb8\xad'", "''''", "'a''b'", "''''''",
    "'a\\b'", "'\\'", "'a\nb'", "'a\rb'", "'a\tb'", "'\001\037\177'",
    "0", "-0", "+0", "0001", "-42", "+42",
    "2147483647", "2147483648", "-2147483648", "-2147483649",
    "9223372036854775807", "-9223372036854775808",
    "9223372036854775808", "-9223372036854775809",
    "9999999999999999999999999999999999999999999",
    "0.0", "1.", ".5", "+.5", "-.5", "+1.0", "-1.25", "000.010",
    "NULL", "null", "NuLl", "nUlL",
    "CURRENT_DATE", "current_date", "CuRrEnT_DaTe",
    "CURRENT_TIME", "current_time", "CuRrEnT_TiMe",
    "CURRENT_TIMESTAMP", "current_timestamp", "CuRrEnT_TiMeStAmP",
    "LOCALTIME", "localtime", "LoCaLtImE",
    "LOCALTIMESTAMP", "localtimestamp", "LoCaLtImEsTaMp",
    "CURRENT_ROLE", "current_role", "CuRrEnT_RoLe",
    "CURRENT_USER", "current_user", "CuRrEnT_UsEr",
    "SESSION_USER", "session_user", "SeSsIoN_UsEr",
    "USER", "user", "UsEr",
    "CURRENT_CATALOG", "current_catalog", "CuRrEnT_CaTaLoG",
    "CURRENT_SCHEMA", "current_schema", "CuRrEnT_ScHeMa",
    ":name", ":name", ":other", ":1", ":12", "?", ":\"a b\"",
    "N'x'", "n'\xce\xa9'", "q'[x''y]'", "NQ'{x}'", "E'a\\nb'", "$$x$$",
    "(1)", "1+2", "coalesce(:name, N'x')", "current_timestamp(3)",
    "nullif(1,2)", "a@remote", "1e3", "1E-3", "0x1", "true", "false",
    "1/*tail*/", "/*head*/1", "1--tail\n", "'a' || 'b'", "'a' 'b'", "'a'b'", "'a' + :p + 'b'",
    "':name ? $1 @x /* */ --'",
    "", " ", "+", "-", ".", "+.", "-.", "1..2", "--1", "++1",
    "'", "'unterminated", "N'broken", "q'[broken", "/*broken", "(1", "1)",
    "nullx", "current_datex", " current_date ", "\t-42\r\n"
};

static const char *const valuefacts_locales[] = {
    "C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US",
    "English_United States.1252", "Turkish_Turkey.1254"
};

#endif
