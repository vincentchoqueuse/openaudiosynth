/* synth_catalog.c — exports every engine and every parameter (range, default, unit, curve, labels,
 * help) straight from the DSP descriptor tables, as Markdown (default) or JSON. This is the reference
 * an AI agent or a UI uses to write patches; regenerate it, never edit it by hand. The JSON output
 * follows the open format spec/schemas/catalog.schema.json (OID 0.2).
 *
 *   synth_catalog            > docs/catalog.md
 *   synth_catalog --json     > docs/catalog.json
 */
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "patch.h"

static void json_str(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

static const char *type_of(const param_desc_t *d)
{
    return d->labels ? "enum" : d->curve == PARAM_INT ? "int" : "float";
}

/* One parameter as an OID 0.2 object: "name": {type, range|choices, default, unit, scale, realtime, cc, role, help} */
static void json_param(const param_desc_t *d, int cc, int last)
{
    printf("        \"%s\": {\"type\": \"%s\", ", d->name, type_of(d));
    if (d->labels) {
        printf("\"choices\": [");
        for (int k = 0; d->labels[k]; k++)
            printf("%s\"%s\"", k ? ", " : "", d->labels[k]);
        printf("], \"default\": \"%s\"", d->labels[(int)d->def]);
    } else {
        printf("\"range\": [%g, %g], \"default\": %g", (double)d->min, (double)d->max, (double)d->def);
        if (d->unit[0])
            printf(", \"unit\": \"%s\"", d->unit);
        if (d->curve == PARAM_EXP)
            printf(", \"scale\": \"log\"");
    }
    printf(", \"realtime\": %s", d->realtime ? "true" : "false");
    if (cc >= 0)
        printf(", \"cc\": %d", cc);
    if (d->role)
        printf(", \"role\": \"%s\"", d->role);
    printf(", \"help\": ");
    json_str(d->help);
    printf("}%s\n", last ? "" : ",");
}

static int engine_cc_for(const engine_ops_t *en, int param)
{
    for (int i = 0; i < en->cc_count; i++)
        if (en->cc_map[i].param == param)
            return en->cc_map[i].cc;
    return -1;
}

static int global_cc_for(const param_desc_t *d)
{
    for (int i = 0; i < synth_midi_cc_count; i++)
        if (!strcmp(synth_midi_cc[i].target, d->name))
            return synth_midi_cc[i].cc;
    return -1;
}

static int is_param_cc(const synth_cc_doc_t *c)
{
    for (int g = 0; g < SYNTH_GLOBAL_COUNT; g++)
        if (!strcmp(synth_global_param(g)->name, c->target))
            return 1;
    return 0;
}

static void print_json(void)
{
    printf("{\n  \"oid\": \"0.3\",\n"
           "  \"instrument\": {\"name\": \"synth\", \"description\": \"modular DSP synthesizer for macOS and STM32: "
           "subtractive, modal/physical and drum machine engines with an effect bus\"},\n"
           "  \"patch_version\": %d,\n",
           PATCH_VERSION);
    printf("  \"midi\": {\"pitch_bend\": \"each engine's bend_range\", \"cc\": {");
    int first = 1;
    for (int i = 0; i < synth_midi_cc_count; i++)
        if (!is_param_cc(&synth_midi_cc[i])) {
            printf("%s\"%d\": \"%s\"", first ? "" : ", ", synth_midi_cc[i].cc, synth_midi_cc[i].target);
            first = 0;
        }
    printf("}},\n  \"global\": {\n    \"description\": \"master and effect bus, shared by every engine: "
           "engine -> chorus -> tape delay -> plate reverb -> master\",\n    \"params\": {\n");
    for (int g = 0; g < SYNTH_GLOBAL_COUNT; g++) {
        const param_desc_t *d = synth_global_param(g);
        json_param(d, global_cc_for(d), g + 1 == SYNTH_GLOBAL_COUNT);
    }
    printf("    }\n  },\n  \"part\": {\n    \"description\": \"mixer strip and routing of each of the %d parts "
           "(multitimbral; a patch sets the part it is loaded on)\",\n    \"params\": {\n", SYNTH_PARTS);
    for (int k = 0; k < SYNTH_PART_COUNT; k++)
        json_param(synth_part_param_info(k), -1, k + 1 == SYNTH_PART_COUNT);
    printf("    }\n  },\n  \"modulation\": {\n    \"description\": \"per part, every engine: 8 macros, 2 LFOs "
           "(free or tempo-synced), 2 MSEGs, MIDI sources, 8 routes onto the engine's realtime parameters (amount "
           "in fractions of the destination's range); route destinations are engine parameter names\",\n"
           "    \"params\": {\n");
    for (int k = 0; k < SYNTH_MOD_COUNT; k++)
        json_param(synth_mod_param_info(k), -1, k + 1 == SYNTH_MOD_COUNT);
    printf("    }\n  },\n  \"engines\": {\n");
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        const engine_ops_t *en = engine_registry[e];
        printf("    \"%s\": {\n      \"description\": ", en->name);
        json_str(en->description);
        printf(",\n      \"voices\": %d,\n", en->voices);
        if (en->note_count) {
            printf("      \"notes\": {");
            for (int i = 0; i < en->note_count; i++)
                printf("%s\"%s\": %d", i ? ", " : "", en->notes[i].name, en->notes[i].note);
            printf("},\n");
        }
        printf("      \"params\": {\n");
        for (int i = 0; i < en->param_count; i++)
            json_param(&en->params[i], engine_cc_for(en, i), i + 1 == en->param_count);
        printf("      }\n    }%s\n", e + 1 < SYNTH_ENGINE_COUNT ? "," : "");
    }
    printf("  }\n}\n");
}

static void md_row(const param_desc_t *d, int cc)
{
    printf("| `%s` | ", d->name);
    if (d->labels) {
        for (int k = 0; d->labels[k]; k++)
            printf("%s%s", k ? ", " : "", d->labels[k]);
        printf(" | %s", d->labels[(int)d->def]);
    } else {
        printf("%g .. %g%s%s | %g", (double)d->min, (double)d->max, d->unit[0] ? " " : "", d->unit, (double)d->def);
    }
    printf(" | %s", d->realtime ? "yes" : "");
    if (cc >= 0)
        printf(" (CC %d)", cc);
    printf(" | `%s` | %s |\n", d->role ? d->role : "", d->help ? d->help : "");
}

static void md_head(void)
{
    printf("| name | range / choices | default | live | role | what it does |\n|---|---|---|---|---|---|\n");
}

static void print_md(void)
{
    printf("# Parameter catalog\n\n"
           "Generated by `synth_catalog` from the DSP descriptor tables: do not edit by hand. Machine-readable\n"
           "version: `docs/catalog.json` (open format OID 0.3, `spec/README.md`).\n\n"
           "Patch values are in **physical units**. **live** = safe to move while playing (CC, modulation);\n"
           "the CC number is the default assignment. **role** = meaning in the shared vocabulary\n"
           "(`spec/roles.json`): a patch may use a role instead of a name. Patch format version: %d.\n",
           PATCH_VERSION);
    printf("\n## Global (every engine): master and effect bus\n\n"
           "Signal flow: engine -> chorus (insert) -> tape delay (send) -> plate reverb (send, also fed by\n"
           "the echoes) -> master -> DC blocker -> soft clipper. All effects are off by default.\n\n");
    md_head();
    for (int g = 0; g < SYNTH_GLOBAL_COUNT; g++)
        md_row(synth_global_param(g), global_cc_for(synth_global_param(g)));
    printf("\nOther MIDI: pitch bend (engine `bend_range`)");
    for (int i = 0; i < synth_midi_cc_count; i++)
        if (!is_param_cc(&synth_midi_cc[i]))
            printf(", CC %d `%s`", synth_midi_cc[i].cc, synth_midi_cc[i].target);
    printf(".\n");
    printf("\n## Part (every engine): mixer strip and routing\n\n"
           "%d parts play at once, each with its own engine, MIDI channel and key range; they share the effect "
           "bus.\n\n", SYNTH_PARTS);
    md_head();
    for (int k = 0; k < SYNTH_PART_COUNT; k++)
        md_row(synth_part_param_info(k), -1);
    printf("\n## Modulation (every engine, per part)\n\n"
           "Sources: macros, part LFOs (`mlfo`), MSEGs, mod wheel, aftertouch, pitch bend, velocity, note, random. "
           "A route adds `amount x source (x via)` to an engine parameter marked live, in fractions of its whole "
           "range. MSEG points 2..16 have the same three parameters as point 1 (`mseg1_t<i>`, `_l<i>`, `_c<i>`).\n\n");
    md_head();
    for (int k = 0; k < SYNTH_MOD_COUNT; k++) {
        const param_desc_t *d = synth_mod_param_info(k);
        const char *u = strrchr(d->name, '_');
        if (!strncmp(d->name, "mseg", 4) && u && (u[1] == 't' || u[1] == 'l' || u[1] == 'c') && strcmp(u + 2, "1") &&
            u[2] >= '0' && u[2] <= '9')
            continue; /* points 2..16: same as point 1 */
        md_row(d, -1);
    }
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        const engine_ops_t *en = engine_registry[e];
        printf("\n## Engine `%s` (%d voices)\n\n%s\n", en->name, en->voices, en->description);
        if (en->note_count) {
            printf("\nNotes:");
            for (int i = 0; i < en->note_count; i++)
                printf("%s %d `%s`", i ? "," : "", en->notes[i].note, en->notes[i].name);
            printf("\n");
        }
        printf("\n");
        md_head();
        for (int i = 0; i < en->param_count; i++)
            md_row(&en->params[i], engine_cc_for(en, i));
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--json"))
        print_json();
    else if (argc > 1) {
        printf("usage: synth_catalog [--json]\n");
        return 2;
    } else
        print_md();
    return 0;
}
