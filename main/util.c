#include <stdbool.h>
#include <stdio.h> 
#include <string.h> 
#include <ctype.h>

bool is_valid_url(const char* str) {
    // Check for NULL input
    if (str == NULL) {
        return false;
    }

    // Minimum length check (needs at least 7 characters for "http://")
    size_t len = strlen(str);
    if (len <= 8) {
        return false;
    }

    // Convert first characters to lowercase for case-insensitive comparison
    char first_letters[9] = {0};
    for (int i = 0; i < 8; i++) {
        first_letters[i] = tolower(str[i]);
    }

    // Check if starts with "http"
    if (strncmp(first_letters, "http://", 7) == 0 || strncmp(first_letters, "https://", 8) == 0) {
        return true;
    }

    return false;
}


#ifdef UNITTEST
#define btoa(x) ((x)?"true":"false")
int main() {
    char url[] = "http://google.fr";
    printf("Test of function is_valid_url\n");
    printf("NULL : expect false; return %s\n", btoa(is_valid_url(NULL)));
    printf("%s : expect true; return %s\n",url, btoa(is_valid_url(url)));
    char url1[] = "https://google.fr";
    printf("%s: expect true; return %s\n", url1, btoa(is_valid_url(url1)));
    char url2[] = "http:/";
    printf("%s: expect false; return %s\n", url2, btoa(is_valid_url(url2)));
    char url3[] = "https:/oki.com";
    printf("%s: expect false; return %s\n", url3, btoa(is_valid_url(url3)));


    return 0;
}
#endif
