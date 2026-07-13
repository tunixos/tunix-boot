#include "config/config.h"

#define NEWLINE '\n'
#define CARRIAGE_RETURN '\r'

static void fail(struct config_error *error, size_t line, const char *reason) {
    if (!error) return;
    error->line = line;
    error->reason = reason;
}

/* The next line, without its terminator. `offset` is left past it. */
static struct str next_line(const char *text, size_t length, size_t *offset) {
    size_t start = *offset;
    size_t end = start;
    while (end < length && text[end] != NEWLINE) end++;

    *offset = end < length ? end + 1U : end;

    size_t stop = end;
    if (stop > start && text[stop - 1U] == CARRIAGE_RETURN) stop--;

    struct str line = {text + start, stop - start};
    return line;
}

static bool split_assignment(struct str line, struct str *key, struct str *value) {
    /* The first separator only: a command line is a value that contains them. */
    for (size_t index = 0; index < line.length; index++) {
        if (line.data[index] != CONFIG_ASSIGNMENT_CHARACTER) continue;

        *key = str_trim(str_slice(line, 0, index));
        *value = str_trim(str_slice(line, index + 1U, line.length - index - 1U));
        return key->length > 0;
    }
    return false;
}

static bool apply_global(struct config *config, struct str key, struct str value,
                         size_t line, struct config_error *error) {
    if (str_equal_cstr(key, "timeout")) {
        if (!str_to_u64(value, &config->timeout_seconds)) {
            fail(error, line, "timeout is not a number");
            return false;
        }
        if (config->timeout_seconds > CONFIG_MAX_TIMEOUT_SECONDS) {
            fail(error, line, "timeout is longer than anyone will wait");
            return false;
        }
        return true;
    }
    if (str_equal_cstr(key, "default")) {
        config->default_entry = value;
        return true;
    }
    fail(error, line, "unknown setting outside an entry");
    return false;
}

static bool apply_entry(struct config_entry *entry, struct str key,
                        struct str value, size_t line,
                        struct config_error *error) {
    if (value.length == 0) {
        fail(error, line, "setting has no value");
        return false;
    }

    if (str_equal_cstr(key, "kernel")) {
        entry->kernel = value;
        return true;
    }
    if (str_equal_cstr(key, "cmdline")) {
        entry->cmdline = value;
        return true;
    }
    if (str_equal_cstr(key, "module")) {
        if (entry->module_count == CONFIG_MAX_MODULES) {
            fail(error, line, "too many modules in one entry");
            return false;
        }
        entry->modules[entry->module_count++] = value;
        return true;
    }
    fail(error, line, "unknown setting in an entry");
    return false;
}

static void clear_entry(struct config_entry *entry) {
    entry->name.data = NULL;
    entry->name.length = 0;
    entry->kernel.data = NULL;
    entry->kernel.length = 0;
    entry->cmdline.data = NULL;
    entry->cmdline.length = 0;
    entry->module_count = 0;
}

bool config_parse(const char *text, size_t length, struct config *out,
                  struct config_error *error) {
    if (!text || !out) return false;

    out->timeout_seconds = CONFIG_DEFAULT_TIMEOUT_SECONDS;
    out->default_entry.data = NULL;
    out->default_entry.length = 0;
    out->entry_count = 0;

    fail(error, 0, NULL);
    if (length > CONFIG_MAX_BYTES) {
        fail(error, 0, "the file is larger than a configuration should ever be");
        return false;
    }

    struct config_entry *current = NULL;
    size_t offset = 0;
    size_t line_number = 0;

    while (offset < length) {
        line_number++;
        struct str line = str_trim(next_line(text, length, &offset));

        if (line.length == 0) continue;
        if (line.data[0] == CONFIG_COMMENT_CHARACTER) continue;
        if (line.length > CONFIG_MAX_LINE_BYTES) {
            fail(error, line_number, "line is too long");
            return false;
        }

        if (line.data[0] == CONFIG_ENTRY_CHARACTER) {
            struct str name = str_trim(str_slice(line, 1, line.length - 1U));
            if (name.length == 0) {
                fail(error, line_number, "entry has no name");
                return false;
            }
            if (out->entry_count == CONFIG_MAX_ENTRIES) {
                fail(error, line_number, "too many entries");
                return false;
            }
            /* Two entries of one name would make `default` ambiguous, and the
               one that wins would be whichever this loop reached first. */
            if (config_find(out, name)) {
                fail(error, line_number, "an entry of this name already exists");
                return false;
            }

            current = &out->entries[out->entry_count++];
            clear_entry(current);
            current->name = name;
            continue;
        }

        struct str key, value;
        if (!split_assignment(line, &key, &value)) {
            fail(error, line_number, "line is neither an entry nor a setting");
            return false;
        }

        bool applied = current ? apply_entry(current, key, value, line_number, error)
                               : apply_global(out, key, value, line_number, error);
        if (!applied) return false;
    }

    /* An entry with no kernel names nothing to boot, and finding that out at
       the moment of booting is finding it out too late. */
    for (unsigned index = 0; index < out->entry_count; index++) {
        if (out->entries[index].kernel.length == 0) {
            fail(error, 0, "an entry has no kernel");
            return false;
        }
    }
    return true;
}

const struct config_entry *config_find(const struct config *config,
                                       struct str name) {
    for (unsigned index = 0; index < config->entry_count; index++) {
        if (str_equal(config->entries[index].name, name))
            return &config->entries[index];
    }
    return NULL;
}

const struct config_entry *config_default_entry(const struct config *config) {
    if (config->entry_count == 0) return NULL;
    if (config->default_entry.length == 0) return &config->entries[0];
    return config_find(config, config->default_entry);
}
