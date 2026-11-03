#include <test.h>

#include <eval_context.h>
#include <cmdb.h>
#include <known_dirs.h>
#include <misc_lib.h>                                          /* xsnprintf */

/* LoadCMDBData() is all-or-nothing: if any entry of host_specific.json cannot
 * be validated or installed, none of the file is installed. */

char CFWORKDIR[CF_BUFSIZE];

void tests_setup(void)
{
    static char env[] = /* Needs to be static for putenv() */
        "CFENGINE_TEST_OVERRIDE_WORKDIR=/tmp/CFENGINE_cmdb_test.XXXXXX";
    char *workdir = strchr(env, '=');
    assert(workdir && workdir[1] == '/');
    workdir++;

    mkdtemp(workdir);
    strlcpy(CFWORKDIR, workdir, CF_BUFSIZE);
    putenv(env);

    mkdir(GetDataDir(), 0700);
}

void tests_teardown(void)
{
    char cmd[CF_BUFSIZE];
    xsnprintf(cmd, CF_BUFSIZE, "rm -rf '%s'", CFWORKDIR);
    system(cmd);
}

static void CMDBFilePath(char *path, size_t size)
{
    xsnprintf(path, size, "%s/host_specific.json", GetDataDir());
}

static void WriteCMDBFile(const char *json)
{
    char path[CF_BUFSIZE];
    CMDBFilePath(path, sizeof(path));

    FILE *f = fopen(path, "w");
    assert_true(f != NULL);
    assert_true(fputs(json, f) >= 0);
    assert_int_equal(fclose(f), 0);
}

static void RemoveCMDBFile(void)
{
    char path[CF_BUFSIZE];
    CMDBFilePath(path, sizeof(path));
    unlink(path);
}

/* The variable's scalar value, or NULL if it is not installed. */
static const char *InstalledScalar(const EvalContext *ctx, const char *ref_str)
{
    VarRef *ref = VarRefParse(ref_str);
    DataType type;
    const void *value = EvalContextVariableGetPlaintext(ctx, ref, &type);
    VarRefDestroy(ref);
    if (value == NULL)
    {
        return NULL;
    }
    assert_int_equal(type, CF_DATA_TYPE_STRING);
    return value;
}

static bool VariableIsInstalled(const EvalContext *ctx, const char *ref_str)
{
    VarRef *ref = VarRefParse(ref_str);
    DataType type;
    const bool found =
        (EvalContextVariableGetPlaintext(ctx, ref, &type) != NULL);
    VarRefDestroy(ref);
    return found;
}

static bool ClassIsInstalled(const EvalContext *ctx, const char *name)
{
    return (EvalContextClassGet(ctx, "data", name) != NULL);
}

static void test_missing_file_is_not_an_error(void)
{
    RemoveCMDBFile();

    EvalContext *ctx = EvalContextNew();
    assert_true(LoadCMDBData(ctx));
    EvalContextDestroy(ctx);
}

static void test_all_good_file_installs_everything(void)
{
    WriteCMDBFile(
        "{\n"
        "  \"vars\": {\n"
        "    \"good_var\": \"from_vars\",\n"
        "    \"good_list\": [\"a\", \"b\"],\n"
        "    \"good_data\": { \"k\": \"v\" }\n"
        "  },\n"
        "  \"variables\": {\n"
        "    \"good_variable\": { \"value\": \"from_variables\",\n"
        "                         \"tags\": [\"t1\"], \"comment\": \"c\" },\n"
        "    \"good_bare\": \"bare\"\n"
        "  },\n"
        "  \"classes\": {\n"
        "    \"good_class\": \"any::\",\n"
        "    \"ns1:good_ns_class\": { \"class_expressions\": [] }\n"
        "  }\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_true(LoadCMDBData(ctx));

    assert_string_equal(InstalledScalar(ctx, "data:variables.good_var"),
                        "from_vars");
    assert_true(VariableIsInstalled(ctx, "data:variables.good_list"));
    assert_true(VariableIsInstalled(ctx, "data:variables.good_data"));
    assert_string_equal(InstalledScalar(ctx, "data:variables.good_variable"),
                        "from_variables");
    assert_string_equal(InstalledScalar(ctx, "data:variables.good_bare"),
                        "bare");
    assert_true(ClassIsInstalled(ctx, "good_class"));
    assert_true(EvalContextClassGet(ctx, "ns1", "good_ns_class") != NULL);

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_bad_var_entry_installs_nothing(void)
{
    /* A JSON null has no value to install; the good entries on both sides of
     * it, and the ones in the other sections, must not be installed either. */
    WriteCMDBFile(
        "{\n"
        "  \"vars\": {\n"
        "    \"first_good_var\": \"x\",\n"
        "    \"bad_var\": null,\n"
        "    \"last_good_var\": \"y\"\n"
        "  },\n"
        "  \"variables\": { \"good_variable\": { \"value\": \"z\" } },\n"
        "  \"classes\": { \"good_class\": \"any::\" }\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    assert_false(VariableIsInstalled(ctx, "data:variables.first_good_var"));
    assert_false(VariableIsInstalled(ctx, "data:variables.bad_var"));
    assert_false(VariableIsInstalled(ctx, "data:variables.last_good_var"));
    assert_false(VariableIsInstalled(ctx, "data:variables.good_variable"));
    assert_false(ClassIsInstalled(ctx, "good_class"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_bad_variables_entry_installs_nothing(void)
{
    /* An entry with an unresolved variable reference, in the 'variables'
     * section, next to good entries in 'vars' and 'variables'. */
    WriteCMDBFile(
        "{\n"
        "  \"vars\": { \"good_var\": \"x\" },\n"
        "  \"variables\": {\n"
        "    \"good_variable\": { \"value\": \"y\" },\n"
        "    \"unresolved\": { \"value\": \"$(sys.workdir)\" }\n"
        "  }\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    assert_false(VariableIsInstalled(ctx, "data:variables.good_var"));
    assert_false(VariableIsInstalled(ctx, "data:variables.good_variable"));
    assert_false(VariableIsInstalled(ctx, "data:variables.unresolved"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_bad_class_entry_installs_nothing(void)
{
    /* Only "any::" is a valid class specification. */
    WriteCMDBFile(
        "{\n"
        "  \"vars\": { \"good_var\": \"x\" },\n"
        "  \"classes\": {\n"
        "    \"first_good_class\": \"any::\",\n"
        "    \"bad_class\": [\"linux::\"],\n"
        "    \"last_good_class\": [\"any::\"]\n"
        "  }\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    assert_false(ClassIsInstalled(ctx, "first_good_class"));
    assert_false(ClassIsInstalled(ctx, "bad_class"));
    assert_false(ClassIsInstalled(ctx, "last_good_class"));
    assert_false(VariableIsInstalled(ctx, "data:variables.good_var"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_bad_metadata_installs_nothing(void)
{
    /* Malformed tags make the entry invalid, not just tag-less. */
    WriteCMDBFile(
        "{\n"
        "  \"vars\": { \"good_var\": \"x\" },\n"
        "  \"variables\": {\n"
        "    \"bad_tags\": { \"value\": \"y\", \"tags\": \"not_an_array\" }\n"
        "  }\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    assert_false(VariableIsInstalled(ctx, "data:variables.good_var"));
    assert_false(VariableIsInstalled(ctx, "data:variables.bad_tags"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_section_of_wrong_type_installs_nothing(void)
{
    WriteCMDBFile(
        "{\n"
        "  \"vars\": { \"good_var\": \"x\" },\n"
        "  \"classes\": [ \"any::\" ]\n"
        "}\n");

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    assert_false(VariableIsInstalled(ctx, "data:variables.good_var"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_reload_does_not_keep_earlier_data(void)
{
    /* What cf-execd, cf-serverd and cf-reactor do on a policy reload: clear
     * the context, then load again. A file that has turned bad since must
     * leave no variables behind, neither old nor new. */
    WriteCMDBFile("{ \"vars\": { \"good_var\": \"old\" } }\n");

    EvalContext *ctx = EvalContextNew();
    assert_true(LoadCMDBData(ctx));
    assert_string_equal(InstalledScalar(ctx, "data:variables.good_var"), "old");

    WriteCMDBFile("{ \"vars\": { \"good_var\": \"new\", \"bad\": null } }\n");
    EvalContextClear(ctx);
    assert_false(LoadCMDBData(ctx));
    assert_false(VariableIsInstalled(ctx, "data:variables.good_var"));

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

int main()
{
    PRINT_TEST_BANNER();
    tests_setup();

    const UnitTest tests[] =
    {
        unit_test(test_missing_file_is_not_an_error),
        unit_test(test_all_good_file_installs_everything),
        unit_test(test_bad_var_entry_installs_nothing),
        unit_test(test_bad_variables_entry_installs_nothing),
        unit_test(test_bad_class_entry_installs_nothing),
        unit_test(test_bad_metadata_installs_nothing),
        unit_test(test_section_of_wrong_type_installs_nothing),
        unit_test(test_reload_does_not_keep_earlier_data),
    };

    int ret = run_tests(tests);

    tests_teardown();

    return ret;
}
