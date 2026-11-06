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

/* Load json, expect failure, and expect none of the named variables (full
 * references) and classes (in the 'data' namespace) to be installed. */
static void assert_rejected_and_nothing_installed(const char *json,
                                                  const char *const *variables,
                                                  const char *const *classes)
{
    WriteCMDBFile(json);

    EvalContext *ctx = EvalContextNew();
    assert_false(LoadCMDBData(ctx));

    for (size_t i = 0; variables != NULL && variables[i] != NULL; i++)
    {
        if (VariableIsInstalled(ctx, variables[i]))
        {
            print_error("variable '%s' is installed by a rejected file\n",
                        variables[i]);
        }
        assert_false(VariableIsInstalled(ctx, variables[i]));
    }
    for (size_t i = 0; classes != NULL && classes[i] != NULL; i++)
    {
        if (ClassIsInstalled(ctx, classes[i]))
        {
            print_error("class '%s' is installed by a rejected file\n",
                        classes[i]);
        }
        assert_false(ClassIsInstalled(ctx, classes[i]));
    }

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_unbalanced_index_key_installs_nothing(void)
{
    /* VarRefParse() answers 'tendcf_interlock[' with a reference to
     * 'tendcf_interlock', which would replace the valid interlock container
     * and leave the service container installed. */
    const char *const absent[] = {
        "data:variables.tendcf_interlock",
        "data:variables.tendcf_service",
        NULL
    };

    /* in 'vars' */
    assert_rejected_and_nothing_installed(
        "{ \"vars\": {\n"
        "    \"tendcf_interlock\": { \"demo\": { \"bundle\": \"demo\" } },\n"
        "    \"tendcf_interlock[\": [],\n"
        "    \"tendcf_service\": { \"svc\": { \"bundle\": \"demo\" } } } }\n",
        absent, NULL);

    /* in 'variables' */
    assert_rejected_and_nothing_installed(
        "{ \"variables\": {\n"
        "    \"tendcf_interlock\": { \"value\": { \"demo\": { \"bundle\": \"demo\" } } },\n"
        "    \"tendcf_interlock[\": { \"value\": [] },\n"
        "    \"tendcf_service\": { \"value\": { \"svc\": { \"bundle\": \"demo\" } } } } }\n",
        absent, NULL);

    /* the interlock in 'variables', the service in 'vars' */
    assert_rejected_and_nothing_installed(
        "{ \"vars\": {\n"
        "    \"tendcf_service\": { \"svc\": { \"bundle\": \"demo\" } } },\n"
        "  \"variables\": {\n"
        "    \"tendcf_interlock\": { \"value\": { \"demo\": { \"bundle\": \"demo\" } } },\n"
        "    \"tendcf_interlock[\": { \"value\": [] } } }\n",
        absent, NULL);

    /* a stray closing bracket */
    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good\": \"x\", \"bad]\": \"y\" } }\n",
        (const char *const[]) { "data:variables.good", NULL }, NULL);
}

static void test_balanced_index_key_installs_nothing(void)
{
    /* An index would make the entry an element of some other variable. */
    const char *const absent[] = { "data:variables.good", NULL };

    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good\": \"x\", \"array[key]\": \"y\" } }\n",
        absent, NULL);
    assert_rejected_and_nothing_installed(
        "{ \"variables\": { \"good\": \"x\", \"array[key]\": { \"value\": \"y\" } } }\n",
        absent, NULL);
}

static void test_frame_dependent_scopes_install_nothing(void)
{
    /* These scopes name tables that exist only while a bundle, body or
     * promise is evaluated (or take no namespace); installing into them
     * during discovery crashes the agent, after earlier entries went in. */
    const char *const keys[] = {
        "default:body.bad", "body.bad", "edit.bad", "default:edit.bad",
        "this.v", "default:this.v", "sys.bad", "default:sys.bad", "const.bad",
        "mon.bad", "match.bad", NULL
    };
    const char *const absent[] = { "data:variables.good", NULL };
    const char *const absent_class[] = { "good_class", NULL };

    for (size_t i = 0; keys[i] != NULL; i++)
    {
        char json[CF_BUFSIZE];

        xsnprintf(json, sizeof(json),
                  "{ \"vars\": { \"good\": \"x\", \"%s\": \"y\" },"
                  "  \"classes\": { \"good_class\": \"any::\" } }\n", keys[i]);
        assert_rejected_and_nothing_installed(json, absent, absent_class);

        xsnprintf(json, sizeof(json),
                  "{ \"variables\": { \"good\": \"x\", \"%s\": { \"value\": \"y\" } },"
                  "  \"classes\": { \"good_class\": \"any::\" } }\n", keys[i]);
        assert_rejected_and_nothing_installed(json, absent, absent_class);
    }
}

static void test_def_scope_is_installed(void)
{
    /* 'def' is no more special than a bundle name (CFE-3668) and works. */
    WriteCMDBFile("{ \"vars\": { \"def.mine\": \"x\", \"mybundle.mine\": \"y\" } }\n");

    EvalContext *ctx = EvalContextNew();
    assert_true(LoadCMDBData(ctx));
    assert_string_equal(InstalledScalar(ctx, "data:def.mine"), "x");
    assert_string_equal(InstalledScalar(ctx, "data:mybundle.mine"), "y");

    EvalContextDestroy(ctx);
    RemoveCMDBFile();
}

static void test_null_section_installs_nothing(void)
{
    /* A section that is present as JSON null is not an absent section. */
    const char *const absent[] = {
        "data:variables.good_var", "data:variables.good_variable", NULL
    };
    const char *const absent_class[] = { "good_class", NULL };

    assert_rejected_and_nothing_installed(
        "{ \"vars\": null,\n"
        "  \"variables\": { \"good_variable\": { \"value\": \"y\" } },\n"
        "  \"classes\": { \"good_class\": \"any::\" } }\n",
        absent, absent_class);
    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good_var\": \"x\" },\n"
        "  \"variables\": null,\n"
        "  \"classes\": { \"good_class\": \"any::\" } }\n",
        absent, absent_class);
    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good_var\": \"x\" },\n"
        "  \"variables\": { \"good_variable\": { \"value\": \"y\" } },\n"
        "  \"classes\": null }\n",
        absent, absent_class);
}

static void test_rule_array_of_wrong_type_installs_nothing(void)
{
    /* The sole element must be a string; an object or array there used to
     * reach an assertion that aborts the agent. */
    const char *const absent[] = { "data:variables.good_var", NULL };
    const char *const absent_class[] = { "good_class", "bad_class", NULL };
    const char *const specs[] = {
        "{ \"class_expressions\": [ {} ] }",
        "{ \"class_expressions\": [ [] ] }",
        "{ \"class_expressions\": [ 1 ] }",
        "{ \"class_expressions\": [ null ] }",
        "{ \"regular_expressions\": [ {} ] }",
        "{ \"regular_expressions\": [ [] ] }",
        "{ \"regular_expressions\": [ true ] }",
        NULL
    };

    for (size_t i = 0; specs[i] != NULL; i++)
    {
        char json[CF_BUFSIZE];
        xsnprintf(json, sizeof(json),
                  "{ \"vars\": { \"good_var\": \"x\" },\n"
                  "  \"classes\": { \"good_class\": \"any::\", \"bad_class\": %s } }\n",
                  specs[i]);
        assert_rejected_and_nothing_installed(json, absent, absent_class);
    }
}

static void test_unknown_top_level_key_installs_nothing(void)
{
    /* A misspelled section would otherwise drop its data and keep the rest. */
    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good_var\": \"x\" },\n"
        "  \"varables\": { \"tendcf_interlock\": { \"value\": \"y\" } },\n"
        "  \"classes\": { \"good_class\": \"any::\" } }\n",
        (const char *const[]) { "data:variables.good_var", NULL },
        (const char *const[]) { "good_class", NULL });
}

static void test_duplicate_variable_installs_nothing(void)
{
    /* One spelling replacing another could swap out a valid entry. */
    const char *const absent[] = { "data:variables.dup", "data:variables.good", NULL };

    assert_rejected_and_nothing_installed(
        "{ \"vars\": { \"good\": \"x\", \"dup\": \"first\" },\n"
        "  \"variables\": { \"data:variables.dup\": { \"value\": \"second\" } } }\n",
        absent, NULL);
}

static bool FailInsertionOfFailMe(const char *key)
{
    return StringEqual(key, "fail_me");
}

static void test_failed_insertion_rolls_back_everything(void)
{
    /* Validation accepts the file, then one variable cannot be inserted
     * after the others went in (forced through the test seam, as no real
     * input gets there). Whatever this load installed must be taken back,
     * and a class that was there before it must be left alone. */
    WriteCMDBFile(
        "{ \"vars\": { \"first\": \"1\", \"fail_me\": \"2\", \"last\": \"3\" },\n"
        "  \"variables\": { \"in_variables\": { \"value\": \"4\" } },\n"
        "  \"classes\": { \"new_class\": \"any::\", \"ns1:new_ns_class\": \"any::\","
        "                 \"pre_class\": \"any::\" } }\n");

    EvalContext *ctx = EvalContextNew();
    assert_true(EvalContextClassPutSoftNS(ctx, "data", "pre_class",
                                          CONTEXT_SCOPE_NAMESPACE, ""));

    CMDBTestFailInsertion = FailInsertionOfFailMe;
    const bool loaded = LoadCMDBData(ctx);
    CMDBTestFailInsertion = NULL;
    assert_false(loaded);

    assert_false(VariableIsInstalled(ctx, "data:variables.first"));
    assert_false(VariableIsInstalled(ctx, "data:variables.fail_me"));
    assert_false(VariableIsInstalled(ctx, "data:variables.last"));
    assert_false(VariableIsInstalled(ctx, "data:variables.in_variables"));
    assert_false(ClassIsInstalled(ctx, "new_class"));
    assert_true(EvalContextClassGet(ctx, "ns1", "new_ns_class") == NULL);
    assert_true(ClassIsInstalled(ctx, "pre_class"));

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
        unit_test(test_unbalanced_index_key_installs_nothing),
        unit_test(test_balanced_index_key_installs_nothing),
        unit_test(test_frame_dependent_scopes_install_nothing),
        unit_test(test_def_scope_is_installed),
        unit_test(test_null_section_installs_nothing),
        unit_test(test_rule_array_of_wrong_type_installs_nothing),
        unit_test(test_unknown_top_level_key_installs_nothing),
        unit_test(test_duplicate_variable_installs_nothing),
        unit_test(test_failed_insertion_rolls_back_everything),
    };

    int ret = run_tests(tests);

    tests_teardown();

    return ret;
}
