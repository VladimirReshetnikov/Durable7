#include <durable7/finger_tree/fingertree.h>

#include <stdio.h>

static int print_char_count(const char* label, const ft_text_rope* rope)
{
    printf("%s: %zu chars, %zu lines\n", label, ft_text_rope_size(rope), ft_text_rope_line_count(rope));
    return 0;
}

int main(void)
{
    ft_text_rope original;
    if (ft_text_rope_from_cstr("one\ntwo\nthree\n", &original) != FT_STATUS_OK) {
        return 1;
    }

    ft_text_rope snapshot;
    if (ft_text_rope_copy(&original, &snapshot) != FT_STATUS_OK) {
        ft_text_rope_dispose(&original);
        return 1;
    }

    ft_text_rope edited;
    if (ft_text_rope_insert_char(&original, 0, '#', &edited) != FT_STATUS_OK) {
        ft_text_rope_dispose(&snapshot);
        ft_text_rope_dispose(&original);
        return 1;
    }

    ft_text_rope restored;
    if (ft_text_rope_remove_at(&edited, 0, &restored) != FT_STATUS_OK) {
        ft_text_rope_dispose(&edited);
        ft_text_rope_dispose(&snapshot);
        ft_text_rope_dispose(&original);
        return 1;
    }

    char first_original = '\0';
    char first_edited = '\0';
    char first_restored = '\0';
    if (ft_text_rope_at(&original, 0, &first_original) != FT_STATUS_OK ||
        ft_text_rope_at(&edited, 0, &first_edited) != FT_STATUS_OK ||
        ft_text_rope_at(&restored, 0, &first_restored) != FT_STATUS_OK) {
        ft_text_rope_dispose(&restored);
        ft_text_rope_dispose(&edited);
        ft_text_rope_dispose(&snapshot);
        ft_text_rope_dispose(&original);
        return 1;
    }

    print_char_count("original", &original);
    print_char_count("snapshot", &snapshot);
    print_char_count("edited", &edited);
    print_char_count("restored", &restored);
    printf("first-chars: %c %c %c\n", first_original, first_edited, first_restored);

    ft_text_rope_dispose(&restored);
    ft_text_rope_dispose(&edited);
    ft_text_rope_dispose(&snapshot);
    ft_text_rope_dispose(&original);
    return 0;
}
