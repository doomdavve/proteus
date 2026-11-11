#include "choreography.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool number(const char *text, float min, float max, float *value)
{
    char *end;
    *value = strtof(text, &end);
    return end != text && !*end && isfinite(*value) && *value >= min && *value <= max;
}

bool choreography_load(const char *path, Choreography *output, char *error, size_t error_size)
{
    typedef struct {
        char name[32];
        ChoreographyMove moves[32];
        int count;
        float groove, beats;
        bool has_groove;
        ChoreographyArms arms;
        int arm_group;
    } Phrase;
    Phrase phrases[8] = { 0 };
    Choreography result = { 0 };
    int phrase_count = 0, active = -1, line_number = 0, arm_group = 0;
    bool performing = false;
    const char *reason = "unable to open choreography file";
    FILE *file = fopen(path, "r");
    if (!file)
        goto invalid;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        if (!strchr(line, '\n') && !feof(file)) {
            reason = "line exceeds 254 characters";
            goto invalid;
        }
        char *comment = strchr(line, '#');
        if (comment)
            *comment = '\0';
        char *words[6];
        int count = 0;
        char *cursor = line;
        while (*cursor) {
            while (isspace((unsigned char)*cursor))
                ++cursor;
            if (!*cursor)
                break;
            if (count == 6) {
                reason = "too many arguments";
                goto invalid;
            }
            words[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor))
                ++cursor;
            if (*cursor)
                *cursor++ = '\0';
        }
        if (!count)
            continue;
        if (strcmp(words[0], "phrase") == 0) {
            reason = "expected: phrase NAME (before play commands; maximum 8 phrases)";
            if (count != 2 || active != -1 || performing || phrase_count == 8
                || strlen(words[1]) >= sizeof(phrases[0].name))
                goto invalid;
            for (int i = 0; i < phrase_count; ++i) {
                if (strcmp(phrases[i].name, words[1]) == 0) {
                    reason = "duplicate phrase name";
                    goto invalid;
                }
            }
            active = phrase_count++;
            strcpy(phrases[active].name, words[1]);
            phrases[active].arms = (ChoreographyArms) { CHOREOGRAPHY_COUNTER, 0, .6f, .12f, 2 };
            phrases[active].arm_group = ++arm_group;
        } else if (strcmp(words[0], "groove") == 0) {
            reason = "expected: groove jack AMOUNT (0..1, once before the phrase's moves)";
            if (count != 3 || active == -1 || strcmp(words[1], "jack") != 0 || phrases[active].count
                || phrases[active].has_groove || !number(words[2], 0, 1, &phrases[active].groove))
                goto invalid;
            phrases[active].has_groove = true;
        } else if (strcmp(words[0], "arms") == 0) {
            reason = "expected: arms counter|sweep|pull left|right AMOUNT DELAY [SPAN] inside a "
                     "phrase";
            if ((count != 5 && count != 6) || active == -1)
                goto invalid;
            ChoreographyArms arms = { 0 };
            if (strcmp(words[1], "counter") == 0)
                arms.type = CHOREOGRAPHY_COUNTER;
            else if (strcmp(words[1], "sweep") == 0)
                arms.type = CHOREOGRAPHY_SWEEP;
            else if (strcmp(words[1], "pull") == 0)
                arms.type = CHOREOGRAPHY_PULL;
            else
                goto invalid;
            if (strcmp(words[2], "left") != 0 && strcmp(words[2], "right") != 0)
                goto invalid;
            arms.lead = strcmp(words[2], "right") == 0;
            reason = "arm amount must be 0..1; delay must be 0..0.25 beats";
            if (!number(words[3], 0, 1, &arms.amount) || !number(words[4], 0, .25f, &arms.delay))
                goto invalid;
            arms.span = 2;
            reason = "arm span must be 2 or 4 beats";
            if (count == 6
                && (!number(words[5], 2, 4, &arms.span) || (arms.span != 2 && arms.span != 4)))
                goto invalid;
            phrases[active].arms = arms;
            phrases[active].arm_group = ++arm_group;
        } else if (strcmp(words[0], "shuffle") == 0 || strcmp(words[0], "side_step") == 0
            || strcmp(words[0], "recover") == 0) {
            bool recover = strcmp(words[0], "recover") == 0;
            reason = "expected: shuffle|side_step SIDE TRAVEL LIFT [BEATS [reset|flow]], or "
                     "recover BEATS";
            if (active == -1 || !phrases[active].has_groove
                || (recover ? count != 2 : (count < 4 || count > 6)))
                goto invalid;
            Phrase *phrase = &phrases[active];
            reason = "maximum 32 moves per phrase";
            if (phrase->count == 32)
                goto invalid;
            ChoreographyMove move = { .type = recover  ? CHOREOGRAPHY_RECOVER
                    : strcmp(words[0], "shuffle") == 0 ? CHOREOGRAPHY_SHUFFLE
                                                       : CHOREOGRAPHY_SIDE_STEP,
                .groove = phrase->groove,
                .arms = phrase->arms,
                .duration = 2,
                .arm_group = phrase->arm_group,
                .source_line = line_number };
            if (recover) {
                reason = "recover duration must be 1 or 2 beats";
                if (!number(words[1], 1, 2, &move.duration)
                    || (move.duration != 1 && move.duration != 2))
                    goto invalid;
            } else {
                reason = "lead must be left or right";
                if (strcmp(words[1], "left") != 0 && strcmp(words[1], "right") != 0)
                    goto invalid;
                move.lead = strcmp(words[1], "right") == 0;
                reason = "travel must be 0.05..0.30 metres; lift must be 0.04..0.18 metres";
                if (!number(words[2], .05f, .30f, &move.travel)
                    || !number(words[3], .04f, .18f, &move.lift))
                    goto invalid;
                reason = "step duration must be 0.5, 1, 2 or 4 beats";
                if (count >= 5
                    && (!number(words[4], .5f, 4, &move.duration)
                        || (move.duration != .5f && move.duration != 1 && move.duration != 2
                            && move.duration != 4)))
                    goto invalid;
                if (count == 6) {
                    reason = "join must be reset or flow";
                    if (strcmp(words[5], "reset") != 0 && strcmp(words[5], "flow") != 0)
                        goto invalid;
                    move.flow = strcmp(words[5], "flow") == 0;
                }
                reason = "fast steps require flow; reduce travel to 0.30 * beats and lift to 0.12 "
                         "* beats";
                if (move.duration < 2
                    && (!move.flow || move.travel > .30f * move.duration
                        || move.lift > .12f * move.duration))
                    goto invalid;
                reason = "flow side_step travel is limited to 0.20 metres for leg reach";
                if (move.flow && move.type == CHOREOGRAPHY_SIDE_STEP && move.travel > .20f)
                    goto invalid;
            }
            strcpy(move.phrase, phrase->name);
            phrase->moves[phrase->count++] = move;
            phrase->beats += move.duration;
        } else if (strcmp(words[0], "end") == 0) {
            reason = "end requires a nonempty phrase filling whole four-beat bars";
            if (count != 1 || active == -1 || !phrases[active].count
                || fmodf(phrases[active].beats, 4) != 0)
                goto invalid;
            active = -1;
        } else if (strcmp(words[0], "play") == 0) {
            float repeats;
            reason = "expected: play NAME REPEATS (integer 1..32, after all phrase definitions)";
            if (count != 3 || active != -1 || !number(words[2], 1, 32, &repeats)
                || repeats != floorf(repeats))
                goto invalid;
            performing = true;
            int index = -1;
            for (int i = 0; i < phrase_count; ++i) {
                if (strcmp(phrases[i].name, words[1]) == 0)
                    index = i;
            }
            reason = "unknown phrase name";
            if (index == -1)
                goto invalid;
            Phrase *phrase = &phrases[index];
            reason = "performance exceeds 256 moves or 128 beats (the starter track length)";
            if (result.count + phrase->count * (int)repeats > CHOREOGRAPHY_MAX_MOVES
                || result.beats + phrase->beats * repeats > 128)
                goto invalid;
            for (int repeat = 0; repeat < (int)repeats; ++repeat) {
                memcpy(result.moves + result.count, phrase->moves,
                    (size_t)phrase->count * sizeof(ChoreographyMove));
                result.count += phrase->count;
                result.beats += phrase->beats;
            }
        } else {
            reason = "unknown command; use phrase, groove, arms, shuffle, side_step, recover, end "
                     "or play";
            goto invalid;
        }
    }
    reason = "read error";
    if (ferror(file))
        goto invalid;
    reason = "unfinished phrase or missing play command";
    if (active != -1 || !result.count)
        goto invalid;
    // Compile stance and timing once; evaluation can then seek directly to any move.
    ChoreographyStance neutral = { { -.22f, .22f }, { 0, 0 }, 0, -1 };
    ChoreographyStance stance = neutral;
    float beat = 0, arm_start = 0;
    for (int i = 0; i < result.count; ++i) {
        ChoreographyMove *move = &result.moves[i];
        line_number = move->source_line;
        if (i && move->arm_group != result.moves[i - 1].arm_group) {
            ChoreographyArms old = result.moves[i - 1].arms;
            reason = "arm cue interrupts a gesture; finish its span before selecting new arms";
            if (old.type != CHOREOGRAPHY_COUNTER && fmodf(beat - arm_start, old.span) != 0)
                goto invalid;
            arm_start = beat;
        }
        move->start = beat;
        move->arm_start = arm_start;
        move->from = stance;
        if (move->flow) {
            reason = "flow must alternate feet: the leading foot is currently supporting; use the "
                     "other foot or recover";
            if (stance.support == move->lead)
                goto invalid;
            float sign = move->lead == 0 ? -1 : 1;
            stance = neutral;
            if (move->type == CHOREOGRAPHY_SHUFFLE) {
                stance.z[move->lead] = move->travel * .5f;
                stance.z[1 - move->lead] = -move->travel * .5f;
            } else {
                stance.x[move->lead] += sign * move->travel;
            }
            stance.support = move->lead;
            stance.root_x = sign * .04f * fminf(move->duration, 2);
        } else if (move->type == CHOREOGRAPHY_RECOVER) {
            reason = "recover requires a preceding flow step";
            if (stance.support == -1)
                goto invalid;
            move->lead = 1 - stance.support;
            stance = neutral;
        } else {
            reason = "reset step requires neutral stance; add recover after flow";
            if (stance.support != -1)
                goto invalid;
        }
        move->to = stance;
        beat += move->duration;
    }
    reason = "loop must return to neutral stance; finish with recover";
    if (stance.support != -1)
        goto invalid;
    ChoreographyMove last = result.moves[result.count - 1];
    reason = "loop cuts off the final arm gesture; finish its span";
    if (last.arms.type != CHOREOGRAPHY_COUNTER && fmodf(beat - arm_start, last.arms.span) != 0)
        goto invalid;
    fclose(file);
    *output = result;
    if (error_size)
        error[0] = '\0';
    return true;
invalid:
    if (file)
        fclose(file);
    snprintf(error, error_size, "%s:%d: %s", path, line_number, reason);
    return false;
}
