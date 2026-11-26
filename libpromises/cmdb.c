/*
  Copyright 2024 Northern.tech AS

  This file is part of CFEngine 3 - written and maintained by Northern.tech AS.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; version 3.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of CFEngine, the applicable Commercial Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#include <platform.h>
#include <ctype.h>
#include <unistd.h>
#include <json.h>
#include <set.h>                /* StringSet */
#include <sequence.h>           /* Seq */
#include <cf3.defs.h>           /* CF_MAXVARSIZE */
#include <class.h>              /* Class */
#include <scope.h>              /* SpecialScopeFromString() */
#include <string_lib.h>
#include <known_dirs.h>         /* GetDataDir() */
#include <var_expressions.h>    /* VarRef, StringContainsUnresolved() */
#include <eval_context.h>       /* EvalContext*() */
#include <files_names.h>        /* JoinPaths() */

#include <host_data.h>
#include <cmdb.h>

#define HOST_SPECIFIC_DATA_FILE "host_specific.json"
#define HOST_SPECIFIC_DATA_MAX_SIZE (5 * 1024 * 1024) /* maximum size of the host-specific.json file */

#define CMDB_NAMESPACE "data"

/* Description of the data used in log messages */
#define CMDB_DATA_DESCRIPTION "CMDB data"

#ifdef TEST_UNIT_TEST
/**
 * Only in the unit test build (cmdb_test.c includes this file with
 * TEST_UNIT_TEST defined, as split_process_line_test.c does for
 * processes_select.c): a variable or class whose CMDB key makes the hook
 * return true is treated as one that cannot be inserted into the EvalContext,
 * which validation cannot otherwise produce. It lets a test exercise the
 * rollback of LoadCMDBData(). Production has no such pointer.
 */
static bool (*CMDBTestFailInsertion)(const char *key) = NULL;
#define CMDBInsertionForcedToFail(key) \
    (CMDBTestFailInsertion != NULL && CMDBTestFailInsertion(key))
#else
#define CMDBInsertionForcedToFail(key) false
#endif

static bool CheckPrimitiveForUnexpandedVars(JsonElement *primitive, ARG_UNUSED void *data)
{
    assert(JsonGetElementType(primitive) == JSON_ELEMENT_TYPE_PRIMITIVE);

    /* Stop the iteration if a variable expression is found. */
    return (!StringContainsUnresolved(JsonPrimitiveGetAsString(primitive)));
}

static bool CheckObjectForUnexpandedVars(JsonElement *object, ARG_UNUSED void *data)
{
    assert(JsonGetType(object) == JSON_TYPE_OBJECT);

    /* Stop the iteration if a variable expression is found among children
     * keys. (elements inside the object are checked separately) */
    JsonIterator iter = JsonIteratorInit(object);
    while (JsonIteratorHasMore(&iter))
    {
        const char *key = JsonIteratorNextKey(&iter);
        if (StringContainsUnresolved(key))
        {
            return false;
        }
    }
    return true;
}

/**
 * Turn a CMDB key into the variable it installs, or NULL (logged) if the key
 * is not a variable this loader can install.
 *
 * The key's syntax is checked here, before VarRefParse(): that function
 * answers a malformed key with a log message and a usable reference to
 * something else ('foo[' becomes 'foo'), which would let an entry silently
 * replace another one. A CMDB key is a variable name, so it holds no index
 * brackets at all.
 *
 * Special scopes are refused too, except 'def': 'this', 'body' and 'edit'
 * name tables that exist only while a promise, body or bundle is being
 * evaluated, and 'sys', 'const', 'mon' and 'match' take no namespace, but
 * every CMDB variable is given one.
 */
static VarRef *GetCMDBVariableRef(const char *key)
{
    if (strpbrk(key, "[]") != NULL)
    {
        Log(LOG_LEVEL_ERR, "Invalid variable specification in %s: '%s'"
            " (a variable name cannot contain '[' or ']')",
            CMDB_DATA_DESCRIPTION, key);
        return NULL;
    }

    VarRef *ref = GetVariableRefFromDataKey(key, CMDB_NAMESPACE, "variables",
                                            CMDB_DATA_DESCRIPTION);
    if (ref == NULL)
    {
        return NULL;
    }

    const SpecialScope special = SpecialScopeFromString(ref->scope);
    if (special != SPECIAL_SCOPE_NONE && special != SPECIAL_SCOPE_DEF)
    {
        Log(LOG_LEVEL_ERR, "Invalid variable specification in %s: '%s'"
            " (the '%s' scope cannot be set from %s)",
            CMDB_DATA_DESCRIPTION, key, ref->scope, CMDB_DATA_DESCRIPTION);
        VarRefDestroy(ref);
        return NULL;
    }

    return ref;
}

/**
 * Check whether a variable can be installed before touching the EvalContext:
 * everything AddCMDBVariable() or EvalContextVariablePutTagsSetWithComment()
 * would reject.
 *
 * @return whether the entry's value can be installed (details are logged
 *         otherwise)
 */
static bool CheckCMDBVariableValue(const char *key, const VarRef *ref,
                                   const JsonElement *data)
{
    assert(key != NULL);
    assert(ref != NULL);
    assert(data != NULL);

    if (strlen(ref->lval) > CF_MAXVARSIZE)
    {
        Log(LOG_LEVEL_ERR, "Invalid variable specification in %s: '%s'"
            " (the variable name exceeds the maximum length allowed, %d characters)",
            CMDB_DATA_DESCRIPTION, key, CF_MAXVARSIZE);
        return false;
    }

    if (JsonGetElementType(data) == JSON_ELEMENT_TYPE_PRIMITIVE &&
        JsonGetPrimitiveType(data) == JSON_PRIMITIVE_TYPE_NULL)
    {
        /* JsonPrimitiveToString() returns NULL for JSON null and there is
         * no scalar to install in its place. */
        Log(LOG_LEVEL_ERR, "Invalid value in '%s' variable specification in %s"
            " (null is not a supported value)", key, CMDB_DATA_DESCRIPTION);
        return false;
    }
    return true;
}

/** Install a variable that CheckCMDBVariableValue() accepted. */
static bool AddCMDBVariable(EvalContext *ctx, const char *key, const VarRef *ref,
                            JsonElement *data, StringSet *tags, const char *comment)
{
    assert(ctx != NULL);
    assert(key != NULL);
    assert(ref != NULL);
    assert(data != NULL);
    assert(tags != NULL);

    if (CMDBInsertionForcedToFail(key))
    {
        Log(LOG_LEVEL_ERR, "Variable '%s' cannot be added (insertion failure forced by a test)", key);
        StringSetDestroy(tags);
        return false;
    }

    bool ret;
    if (JsonGetElementType(data) == JSON_ELEMENT_TYPE_PRIMITIVE)
    {
        assert(JsonGetPrimitiveType(data) != JSON_PRIMITIVE_TYPE_NULL);
        char *value = JsonPrimitiveToString(data);
        Log(LOG_LEVEL_VERBOSE, "Installing CMDB variable '%s:%s.%s=%s'",
            ref->ns, ref->scope, key, value);
        ret = EvalContextVariablePutTagsSetWithComment(ctx, ref, value, CF_DATA_TYPE_STRING,
                                                       tags, comment);
        free(value);
    }
    else if ((JsonGetType(data) == JSON_TYPE_ARRAY) &&
             JsonArrayContainsOnlyPrimitives(data))
    {
        // map to slist if the data only has primitives
        Log(LOG_LEVEL_VERBOSE, "Installing CMDB slist variable '%s:%s.%s'",
            ref->ns, ref->scope, key);
        Rlist *data_rlist = RlistFromContainer(data);
        ret = EvalContextVariablePutTagsSetWithComment(ctx, ref,
                                                       data_rlist, CF_DATA_TYPE_STRING_LIST,
                                                       tags, comment);
        RlistDestroy(data_rlist);
    }
    else
    {
        // install as a data container
        Log(LOG_LEVEL_VERBOSE, "Installing CMDB data container variable '%s:%s.%s'",
            ref->ns, ref->scope, key);
        ret = EvalContextVariablePutTagsSetWithComment(ctx, ref,
                                                       data, CF_DATA_TYPE_CONTAINER,
                                                       tags, comment);
    }
    if (!ret)
    {
        /* On success, EvalContextVariablePutTagsSet() consumes the tags set,
         * otherwise, we shall destroy it. */
        StringSetDestroy(tags);
    }
    return ret;
}

/*
 * The CMDB file is loaded in two steps. First every entry of every section is
 * validated and prepared, without touching the EvalContext; only if all of
 * them are fine is anything installed. A host-specific file is one document
 * written by one author, so a single bad entry means the file as a whole
 * cannot be trusted, and installing only the entries around it could leave
 * policy running without data it expects to guard it (see LoadCMDBData()).
 */
typedef struct
{
    const char *key;            /* borrowed from the JSON document */
    VarRef *ref;                /* owned */
    JsonElement *data;          /* borrowed from the JSON document */
    StringSet *tags;            /* owned until installed */
    const char *comment;        /* borrowed from the JSON document, or NULL */
} CMDBPendingVariable;

typedef struct
{
    const char *key;            /* borrowed from the JSON document */
    StringSet *tags;            /* owned until installed */
    const char *comment;        /* borrowed from the JSON document, or NULL */
    bool installed;             /* set by InstallCMDBPending() */
} CMDBPendingClass;

typedef struct
{
    Seq *variables;             /* CMDBPendingVariable *, in install order */
    Seq *classes;               /* CMDBPendingClass *, in install order */
} CMDBPending;

static void CMDBPendingVariableDestroy(void *item)
{
    CMDBPendingVariable *var = item;
    VarRefDestroy(var->ref);
    StringSetDestroy(var->tags);
    free(var);
}

static void CMDBPendingClassDestroy(void *item)
{
    CMDBPendingClass *cls = item;
    StringSetDestroy(cls->tags);
    free(cls);
}

static CMDBPending CMDBPendingNew(void)
{
    return (CMDBPending) {
        .variables = SeqNew(16, CMDBPendingVariableDestroy),
        .classes = SeqNew(16, CMDBPendingClassDestroy),
    };
}

static void CMDBPendingDestroy(CMDBPending *pending)
{
    SeqDestroy(pending->variables);
    SeqDestroy(pending->classes);
}

/**
 * Refuse two entries that name the same variable (also across sections and
 * spellings: 'x', 'data:variables.x' and 'variables.x' are one variable): the
 * later would replace the earlier one, so an entry could silently take the
 * place of the one a policy relies on.
 *
 * @return false (logged) if any variable is named more than once
 */
static bool CMDBPendingCheckNoDuplicates(const CMDBPending *pending)
{
    bool valid = true;
    StringSet *seen = StringSetNew();
    for (size_t i = 0; i < SeqLength(pending->variables); i++)
    {
        const CMDBPendingVariable *var = SeqAt(pending->variables, i);
        char *address = VarRefToString(var->ref, true);
        if (StringSetContains(seen, address))
        {
            Log(LOG_LEVEL_ERR,
                "Invalid variable specification in %s: '%s' names the variable '%s'"
                " that another entry names too", CMDB_DATA_DESCRIPTION, var->key, address);
            valid = false;
            free(address);
        }
        else
        {
            StringSetAdd(seen, address); /* takes ownership */
        }
    }
    StringSetDestroy(seen);
    return valid;
}

/**
 * Is rules an array that is empty or holds just the string 'allowed'? The
 * element's type is checked before its value is read.
 */
static bool CheckCMDBRuleArray(const JsonElement *rules, const char *allowed)
{
    if (JsonGetType(rules) != JSON_TYPE_ARRAY || JsonLength(rules) > 1)
    {
        return false;
    }
    if (JsonLength(rules) == 0)
    {
        return true;
    }
    const JsonElement *rule = JsonArrayGet(rules, 0);
    return (JsonGetType(rule) == JSON_TYPE_STRING &&
            StringEqual(JsonPrimitiveGetAsString(rule), allowed));
}

/** Takes ownership of ref and tags. */
static void CMDBPendingAddVariable(CMDBPending *pending, const char *key,
                                   VarRef *ref, JsonElement *data,
                                   StringSet *tags, const char *comment)
{
    CMDBPendingVariable *var = xmalloc(sizeof(*var));
    *var = (CMDBPendingVariable) {
        .key = key, .ref = ref, .data = data, .tags = tags, .comment = comment,
    };
    SeqAppend(pending->variables, var);
}

/** Takes ownership of tags. */
static void CMDBPendingAddClass(CMDBPending *pending, const char *key,
                                StringSet *tags, const char *comment)
{
    CMDBPendingClass *cls = xmalloc(sizeof(*cls));
    *cls = (CMDBPendingClass) { .key = key, .tags = tags, .comment = comment };
    SeqAppend(pending->classes, cls);
}

/**
 * Validate the 'vars' section.
 *
 * @return false if the section or any entry in it is invalid. Every entry is
 *         looked at regardless, so that all problems are logged in one go.
 */
static bool PrepareCMDBVars(JsonElement *vars, CMDBPending *pending)
{
    assert(vars != NULL);

    if (JsonGetType(vars) != JSON_TYPE_OBJECT)
    {
        Log(LOG_LEVEL_ERR, "Invalid 'vars' CMDB data, must be a JSON object");
        return false;
    }

    if (!JsonWalk(vars, CheckObjectForUnexpandedVars, NULL, CheckPrimitiveForUnexpandedVars, NULL))
    {
        Log(LOG_LEVEL_ERR, "Invalid 'vars' CMDB data, cannot contain variable references");
        return false;
    }

    bool all_valid = true;
    JsonIterator iter = JsonIteratorInit(vars);
    while (JsonIteratorHasMore(&iter))
    {
        const char *key = JsonIteratorNextKey(&iter);
        JsonElement *data = JsonObjectGet(vars, key);

        VarRef *ref = GetCMDBVariableRef(key);
        if (ref == NULL)
        {
            all_valid = false;
            continue;
        }

        if (!CheckCMDBVariableValue(key, ref, data))
        {
            VarRefDestroy(ref);
            all_valid = false;
            continue;
        }

        StringSet *tags = StringSetNew();
        StringSetAdd(tags, xstrdup(CMDB_SOURCE_TAG));
        CMDBPendingAddVariable(pending, key, ref, data, tags, NULL);
    }
    return all_valid;
}

/**
 * Read the tags of an entry; *valid is cleared if they are malformed. The
 * returned set is never NULL and always has CMDB_SOURCE_TAG in it.
 */
static StringSet *GetCMDBTags(const char *item_type,
                              const char *key,
                              const JsonElement *json_tags,
                              bool *valid)
{
    StringSet *tags = NULL;
    if (JSON_NOT_NULL(json_tags))
    {
        if ((JsonGetType(json_tags) != JSON_TYPE_ARRAY) ||
            (!JsonArrayContainsOnlyPrimitives((JsonElement *) json_tags)))
        {
            Log(LOG_LEVEL_ERR,
                "Invalid tags information for %s '%s' in %s:"
                " must be a JSON array of strings",
                item_type, key, CMDB_DATA_DESCRIPTION);
            *valid = false;
        }
        else
        {
            tags = JsonArrayToStringSet(json_tags);
            if (tags == NULL)
            {
                Log(LOG_LEVEL_ERR,
                    "Invalid tags information for %s '%s' in %s:"
                    " must be a JSON array of strings",
                    item_type, key, CMDB_DATA_DESCRIPTION);
                *valid = false;
            }
        }
    }
    if (tags == NULL)
    {
        tags = StringSetNew();
    }
    StringSetAdd(tags, xstrdup(CMDB_SOURCE_TAG));

    return tags;
}

/** Read the comment of an entry; *valid is cleared if it is not a string. */
static const char *GetCMDBComment(const char *item_type, const char *identifier,
                                  const JsonElement *json_object,
                                  bool *valid)
{
    assert(JsonGetType(json_object) == JSON_TYPE_OBJECT);

    JsonElement *json_comment = JsonObjectGet(json_object, JSON_DATA_KEY_COMMENT);
    if (NULL_JSON(json_comment))
    {
        return NULL;
    }

    if (JsonGetType(json_comment) != JSON_TYPE_STRING)
    {
        Log(LOG_LEVEL_ERR,
            "Invalid type of the 'comment' field for the '%s' %s in %s, must be a string",
            identifier, item_type, CMDB_DATA_DESCRIPTION);
        *valid = false;
        return NULL;
    }

    return JsonPrimitiveGetAsString(json_comment);
}

/**
 * Validate the 'variables' section, which uses the new format allowing
 * metadata (CFE-3633). Same contract as PrepareCMDBVars().
 */
static bool PrepareCMDBVariables(JsonElement *variables, CMDBPending *pending)
{
    assert(variables != NULL);

    if (JsonGetType(variables) != JSON_TYPE_OBJECT)
    {
        Log(LOG_LEVEL_ERR, "Invalid 'variables' CMDB data, must be a JSON object");
        return false;
    }

    if (!JsonWalk(variables, CheckObjectForUnexpandedVars, NULL, CheckPrimitiveForUnexpandedVars, NULL))
    {
        Log(LOG_LEVEL_ERR, "Invalid 'variables' CMDB data, cannot contain variable references");
        return false;
    }

    bool all_valid = true;
    JsonIterator iter = JsonIteratorInit(variables);
    while (JsonIteratorHasMore(&iter))
    {
        const char *key = JsonIteratorNextKey(&iter);

        VarRef *ref = GetCMDBVariableRef(key);
        if (ref == NULL)
        {
            all_valid = false;
            continue;
        }

        JsonElement *const var_info = JsonObjectGet(variables, key);

        JsonElement *data;
        StringSet *tags;
        const char *comment = NULL;
        bool entry_valid = true;

        if (JsonGetType(var_info) == JSON_TYPE_OBJECT)
        {
            data = JsonObjectGet(var_info, JSON_DATA_KEY_VALUE);

            if (data == NULL)
            {
                Log(LOG_LEVEL_ERR, "Missing value in '%s' variable specification in CMDB data (value field is required)", key);
                VarRefDestroy(ref);
                all_valid = false;
                continue;
            }

            JsonElement *json_tags = JsonObjectGet(var_info, JSON_DATA_KEY_TAGS);
            tags = GetCMDBTags("variable", key, json_tags, &entry_valid);
            comment = GetCMDBComment("variable", key, var_info, &entry_valid);
        }
        else
        {
            // Just a bare value, like in "vars", no metadata
            data = var_info;
            tags = StringSetNew();
            StringSetAdd(tags, xstrdup(CMDB_SOURCE_TAG));
        }

        assert(tags != NULL);
        assert(data != NULL);

        if (!entry_valid || !CheckCMDBVariableValue(key, ref, data))
        {
            StringSetDestroy(tags);
            VarRefDestroy(ref);
            all_valid = false;
            continue;
        }

        CMDBPendingAddVariable(pending, key, ref, data, tags, comment);
    }
    return all_valid;
}

/** Split a class key the way AddCMDBClass() does: "ns:name" or "name". */
static const char *CMDBClassNameOfKey(const char *key)
{
    const char *sep = strchr(key, ':');
    return (sep == NULL) ? key : (sep + 1);
}

/**
 * Does EvalContextClassPutTagsSet() have a name it can work with?
 *
 * That function chops trailing whitespace off a copy of the name and refuses
 * the class if nothing is left (in the default namespace), yet files the class
 * under the name as given, whitespace canonified to '_'. A name with
 * whitespace at either end is thus either refused there or filed under a name
 * its author did not write, so it is refused here.
 */
static bool CheckCMDBClassName(const char *key)
{
    const char *name = CMDBClassNameOfKey(key);
    if (name[0] == '\0')
    {
        Log(LOG_LEVEL_ERR, "Invalid CMDB class '%s': the class name is empty", key);
        return false;
    }
    if (isspace((unsigned char) name[0]) ||
        isspace((unsigned char) name[strlen(name) - 1]))
    {
        Log(LOG_LEVEL_ERR,
            "Invalid CMDB class '%s': the class name starts or ends with whitespace",
            key);
        return false;
    }
    if (strlen(name) >= CF_MAXVARSIZE)
    {
        Log(LOG_LEVEL_ERR,
            "Invalid CMDB class '%s': the class name is equal or longer than %d characters",
            key, CF_MAXVARSIZE);
        return false;
    }
    return true;
}

static bool AddCMDBClass(EvalContext *ctx, const char *key, StringSet *tags, const char *comment)
{
    assert(ctx != NULL);
    assert(key != NULL);
    assert(tags != NULL);

    if (CMDBInsertionForcedToFail(key))
    {
        Log(LOG_LEVEL_ERR, "Class '%s' cannot be added (insertion failure forced by a test)", key);
        StringSetDestroy(tags);
        return false;
    }

    bool ret;
    Log(LOG_LEVEL_VERBOSE, "Installing CMDB class '%s'", key);

    if (strchr(key, ':') != NULL)
    {
        char *ns_class_name = xstrdup(key);
        char *sep = strchr(ns_class_name, ':');
        *sep = '\0';
        key = sep + 1;
        ret = EvalContextClassPutSoftNSTagsSetWithComment(ctx, ns_class_name, key,
                                                          CONTEXT_SCOPE_NAMESPACE, tags, comment);
        free(ns_class_name);
    }
    else
    {
        ret = EvalContextClassPutSoftNSTagsSetWithComment(ctx, CMDB_NAMESPACE, key,
                                                          CONTEXT_SCOPE_NAMESPACE, tags, comment);
    }
    if (!ret)
    {
        /* On success, EvalContextClassPutSoftNSTagsSetWithComment() consumes
         * the tags set, otherwise, we shall destroy it. */
        StringSetDestroy(tags);
    }

    return ret;
}

/**
 * Validate the 'classes' section. Same contract as PrepareCMDBVars().
 */
static bool PrepareCMDBClasses(JsonElement *classes, CMDBPending *pending)
{
    assert(classes != NULL);

    if (JsonGetType(classes) != JSON_TYPE_OBJECT)
    {
        Log(LOG_LEVEL_ERR, "Invalid 'classes' CMDB data, must be a JSON object");
        return false;
    }

    if (!JsonWalk(classes, CheckObjectForUnexpandedVars, NULL, CheckPrimitiveForUnexpandedVars, NULL))
    {
        Log(LOG_LEVEL_ERR, "Invalid 'classes' CMDB data, cannot contain variable references");
        return false;
    }

    bool all_valid = true;
    JsonIterator iter = JsonIteratorInit(classes);
    while (JsonIteratorHasMore(&iter))
    {
        const char *key = JsonIteratorNextKey(&iter);
        JsonElement *data = JsonObjectGet(classes, key);

        if (!CheckCMDBClassName(key))
        {
            all_valid = false;
            continue;
        }

        StringSet *tags = NULL;
        const char *comment = NULL;

        if (JsonGetElementType(data) == JSON_ELEMENT_TYPE_PRIMITIVE)
        {
            const char *expr = JsonPrimitiveGetAsString(data);
            if (!StringEqual(expr, "any::"))
            {
                Log(LOG_LEVEL_ERR,
                    "Invalid class specification '%s' in CMDB data, only \"any::\" allowed", expr);
                all_valid = false;
                continue;
            }

            tags = StringSetNew();
            StringSetAdd(tags, xstrdup(CMDB_SOURCE_TAG));
        }
        else if (JsonGetContainerType(data) == JSON_CONTAINER_TYPE_ARRAY &&
                 JsonArrayContainsOnlyPrimitives(data))
        {
            bool array_valid = true;
            switch (JsonLength(data))
            {
            case 0:
                Log(LOG_LEVEL_ERR,
                    "Empty class specification '[]' in CMDB data not allowed, only '[\"any::\"]'");
                array_valid = false;
                break;
            case 1:
                if (!StringEqual(JsonPrimitiveGetAsString(JsonArrayGet(data, 0)), "any::"))
                {
                    Log(LOG_LEVEL_ERR,
                        "Invalid class specification '[\"%s\"]' in CMDB data, only '[\"any::\"]' allowed",
                        JsonPrimitiveGetAsString(JsonArrayGet(data, 0)));
                    array_valid = false;
                }
                // else all good :)
                break;
            default:
                {
                    Writer *const str = StringWriter();
                    JsonWriteCompact(str, data);
                    Log(LOG_LEVEL_ERR,
                        "Too many elements in class specification '%s' in CMDB data, only '[\"any::\"]' allowed",
                        StringWriterData(str));
                    WriterClose(str);
                }
                array_valid = false;
                break;
            }
            if (!array_valid)
            {
                all_valid = false;
                continue;
            }

            tags = StringSetNew();
            StringSetAdd(tags, xstrdup(CMDB_SOURCE_TAG));
        }
        else if (JsonGetContainerType(data) == JSON_CONTAINER_TYPE_OBJECT)
        {
            const JsonElement *class_exprs = JsonObjectGet(data, JSON_DATA_KEY_CLASS_EXPRESSIONS);
            const JsonElement *reg_exprs = JsonObjectGet(data, JSON_DATA_KEY_REGULAR_EXPRESSIONS);
            const JsonElement *json_tags = JsonObjectGet(data, JSON_DATA_KEY_TAGS);

            if (JSON_NOT_NULL(class_exprs) &&
                !CheckCMDBRuleArray(class_exprs, "any::"))
            {
                Log(LOG_LEVEL_ERR,
                    "Invalid class expression rules for class '%s' in CMDB data,"
                    " only '[]' or '[\"any::\"]' allowed", key);
                all_valid = false;
                continue;
            }
            if (JSON_NOT_NULL(reg_exprs) &&
                !CheckCMDBRuleArray(reg_exprs, "any"))
            {
                Log(LOG_LEVEL_ERR,
                    "Invalid regular expression rules for class '%s' in CMDB data,"
                    " only '[]' or '[\"any\"]' allowed", key);
                all_valid = false;
                continue;
            }

            bool entry_valid = true;
            tags = GetCMDBTags("class", key, json_tags, &entry_valid);
            comment = GetCMDBComment("class", key, data, &entry_valid);
            if (!entry_valid)
            {
                StringSetDestroy(tags);
                all_valid = false;
                continue;
            }
        }
        else
        {
            Log(LOG_LEVEL_ERR, "Invalid CMDB class data for class '%s'", key);
            all_valid = false;
            continue;
        }

        CMDBPendingAddClass(pending, key, tags, comment);
    }
    return all_valid;
}

/**
 * The namespace and name EvalContextClassPutTagsSet() files a CMDB class
 * under: the key split the way AddCMDBClass() splits it, the name canonified
 * the way ClassInit() does. Both results are owned by the caller.
 */
static void CMDBClassIdentity(const char *key, char **ns, char **name)
{
    const char *sep = strchr(key, ':');
    *ns = (sep != NULL) ? xstrndup(key, sep - key) : xstrdup(CMDB_NAMESPACE);
    /* Not CanonifyName(): it returns a static buffer */
    *name = xstrdup(CMDBClassNameOfKey(key));
    CanonifyNameInPlace(*name);
}

/**
 * Is the class there already? EvalContextClassPutTagsSet() decides that by the
 * name as given, so 'pre-class' is not found where 'pre_class' is, and the
 * existing class would be replaced; ask by the canonical identity instead.
 */
static bool CMDBClassExists(const EvalContext *ctx, const char *key)
{
    char *ns;
    char *name;
    CMDBClassIdentity(key, &ns, &name);
    const Class *existing = EvalContextClassGet(ctx, ns, name);
    free(ns);
    free(name);
    return (existing != NULL && existing->scope == CONTEXT_SCOPE_NAMESPACE);
}

/**
 * Refuse a variable that exists already: installing it would replace the
 * existing one, and RollbackCMDBPending() could not put that one back.
 * Nothing is there to be replaced in practice, as the loader runs on a
 * cleared context, before augments.
 *
 * @return false (logged) if any variable exists already
 */
static bool CMDBPendingCheckNoExistingVariables(const EvalContext *ctx,
                                                const CMDBPending *pending)
{
    bool valid = true;
    for (size_t i = 0; i < SeqLength(pending->variables); i++)
    {
        const CMDBPendingVariable *var = SeqAt(pending->variables, i);
        /* The value of an empty list is NULL, so go by the type */
        DataType type = CF_DATA_TYPE_NONE;
        EvalContextVariableGetPlaintext(ctx, var->ref, &type);
        if (type != CF_DATA_TYPE_NONE)
        {
            char *address = VarRefToString(var->ref, true);
            Log(LOG_LEVEL_ERR,
                "Invalid variable specification in %s: '%s' names the"
                " variable '%s' that exists already",
                CMDB_DATA_DESCRIPTION, var->key, address);
            free(address);
            valid = false;
        }
    }
    return valid;
}

/**
 * Take back what InstallCMDBPending() installed so far: the first
 * variables_installed variables and every class marked installed. A class
 * that was already there was not installed by this load and is left alone.
 *
 * Removing is restoring only because nothing it removes was there before:
 * LoadCMDBData() refuses a variable that exists already, and
 * InstallCMDBPending() does not touch a class that does (by its canonical
 * identity, so an alias of one is left alone too).
 */
static void RollbackCMDBPending(EvalContext *ctx, CMDBPending *pending,
                                size_t variables_installed)
{
    for (size_t i = 0; i < variables_installed; i++)
    {
        const CMDBPendingVariable *var = SeqAt(pending->variables, i);
        EvalContextVariableRemove(ctx, var->ref);
    }

    for (size_t i = 0; i < SeqLength(pending->classes); i++)
    {
        const CMDBPendingClass *cls = SeqAt(pending->classes, i);
        if (!cls->installed)
        {
            continue;
        }

        char *ns;
        char *name;
        CMDBClassIdentity(cls->key, &ns, &name);
        EvalContextClassRemove(ctx, ns, name);
        free(ns);
        free(name);
    }
}

/**
 * Install what PrepareCMDB*() accepted. Everything that can make an
 * installation fail has been checked by then, except for one case: a class
 * that already exists is not an error and is left as it is.
 *
 * All or nothing in its own right, should the validation have a gap: at the
 * first class or variable that cannot be installed, everything this call
 * installed is removed again and false is returned.
 *
 * @return false if a class or variable could not be installed after all
 *         (details are logged); nothing is left installed then
 */
static bool InstallCMDBPending(EvalContext *ctx, CMDBPending *pending)
{
    /* Classes first: installing them before the variables lets a failing
     * variable exercise the rollback of both. */
    for (size_t i = 0; i < SeqLength(pending->classes); i++)
    {
        CMDBPendingClass *cls = SeqAt(pending->classes, i);
        if (CMDBClassExists(ctx, cls->key))
        {
            /* Not an error, and not ours to replace or to take back */
            Log(LOG_LEVEL_VERBOSE,
                "Not adding CMDB class '%s', it exists already", cls->key);
            continue;
        }

        cls->installed = AddCMDBClass(ctx, cls->key, cls->tags, cls->comment);
        cls->tags = NULL;       /* consumed or destroyed by AddCMDBClass() */
        if (!cls->installed)
        {
            /* Details should have been logged already. */
            Log(LOG_LEVEL_ERR,
                "Failed to add CMDB class '%s', removing what was installed from the file",
                cls->key);
            RollbackCMDBPending(ctx, pending, 0);
            return false;
        }
    }

    for (size_t i = 0; i < SeqLength(pending->variables); i++)
    {
        CMDBPendingVariable *var = SeqAt(pending->variables, i);
        bool ret = AddCMDBVariable(ctx, var->key, var->ref, var->data,
                                   var->tags, var->comment);
        var->tags = NULL;       /* consumed or destroyed by AddCMDBVariable() */
        if (!ret)
        {
            /* Details should have been logged already. */
            Log(LOG_LEVEL_ERR,
                "Failed to add CMDB variable '%s', removing what was installed from the file",
                var->key);
            RollbackCMDBPending(ctx, pending, i);
            return false;
        }
    }
    return true;
}

/**
 * Load host_specific.json into the EvalContext, all or nothing.
 *
 * If the file is there but anything in it is unusable (unreadable, not
 * valid JSON, not an object, a section of the wrong type, or any single
 * entry of 'vars', 'variables' or 'classes' that cannot be validated or
 * installed), nothing at all is installed from it, every problem found is
 * logged, and false is returned.
 *
 * An absent file is not an error. A top-level key other than 'vars',
 * 'variables' and 'classes' makes the file invalid, as does a section whose
 * value is not a JSON object (JSON null included), two entries that name
 * the same variable, or a variable that exists already. A class that exists
 * already is left as it is.
 */
bool LoadCMDBData(EvalContext *ctx)
{
    char file_path[PATH_MAX] = {0};
    strncpy(file_path, GetDataDir(), sizeof(file_path) - 1);
    JoinPaths(file_path, sizeof(file_path), HOST_SPECIFIC_DATA_FILE);
    if (access(file_path, F_OK) != 0)
    {
        Log(LOG_LEVEL_VERBOSE, "No host-specific JSON data available at '%s'", file_path);
        return true;            /* not an error */
    }
    if (access(file_path, R_OK) != 0)
    {
        Log(LOG_LEVEL_ERR, "Cannot read host-spefic JSON data from '%s'",
            file_path);
        return false;
    }

    JsonElement *data = ReadJsonFile(file_path, LOG_LEVEL_ERR, HOST_SPECIFIC_DATA_MAX_SIZE);
    if (data == NULL)
    {
        /* Details are logged by ReadJsonFile() */
        return false;
    }
    if (JsonGetType(data) != JSON_TYPE_OBJECT)
    {
        Log(LOG_LEVEL_ERR, "Invalid CMDB contents in '%s', must be a JSON object", file_path);
        JsonDestroy(data);
        return false;
    }

    Log(LOG_LEVEL_VERBOSE, "Loaded CMDB data file '%s', validating contents", file_path);

    bool unknown_key_found = false;
    JsonIterator iter = JsonIteratorInit(data);
    while (JsonIteratorHasMore(&iter))
    {
        const char *key = JsonIteratorNextKey(&iter);
        /* Only vars, variables and classes allowed in CMDB data. */
        if (!IsStrIn(key, (const char *[4]){"vars", "classes", "variables", NULL}))
        {
            Log(LOG_LEVEL_ERR, "Invalid key '%s' in the CMDB data file '%s'"
                " (only 'vars', 'variables' and 'classes' are allowed)",
                key, file_path);
            unknown_key_found = true;
        }
    }

    /* Step 1: validate everything, install nothing. Every section is
     * looked at even after a failure, so the log names every bad entry.
     * A section is there if its key is, whatever it holds: a JSON null is
     * not "absent" and PrepareCMDB*() refuse it as not being an object. */
    CMDBPending pending = CMDBPendingNew();
    bool valid = !unknown_key_found;
    JsonElement *vars = JsonObjectGet(data, "vars");
    if (vars != NULL && !PrepareCMDBVars(vars, &pending))
    {
        valid = false;
    }
    /* Uses the new format allowing metadata (CFE-3633) */
    JsonElement *variables = JsonObjectGet(data, "variables");
    if (variables != NULL && !PrepareCMDBVariables(variables, &pending))
    {
        valid = false;
    }
    JsonElement *classes = JsonObjectGet(data, "classes");
    if (classes != NULL && !PrepareCMDBClasses(classes, &pending))
    {
        valid = false;
    }
    if (!CMDBPendingCheckNoDuplicates(&pending))
    {
        valid = false;
    }
    if (!CMDBPendingCheckNoExistingVariables(ctx, &pending))
    {
        valid = false;
    }

    /* Step 2: install, but only if the whole file is good. */
    bool success = false;
    if (valid)
    {
        Log(LOG_LEVEL_VERBOSE, "Installing contents of CMDB data file '%s'", file_path);
        success = InstallCMDBPending(ctx, &pending);
    }
    else
    {
        Log(LOG_LEVEL_ERR,
            "CMDB data file '%s' has invalid entries (see above), installing none of its contents",
            file_path);
    }

    CMDBPendingDestroy(&pending);
    JsonDestroy(data);
    return success;
}
