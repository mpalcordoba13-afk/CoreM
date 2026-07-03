#include "users.h"

#define MAX_USERS 4

typedef struct {
    char name[16];
    char pass[16];
    int  active;
} user_t;

static user_t users[MAX_USERS];
static int user_count = 0;
static char current_user[16] = "";

static void scpy(char *d, const char *s, int max) {
    int i=0; while (s[i] && i<max-1) { d[i]=s[i]; i++; } d[i]='\0';
}
static int seq(const char *a, const char *b) {
    while (*a && *b) { if (*a!=*b) return 0; a++; b++; }
    return *a==*b;
}

void users_init(void) {
    user_count = 0;
    scpy(users[user_count].name,"admin",16); scpy(users[user_count].pass,"1234",16);
    users[user_count].active=1; user_count++;

    scpy(users[user_count].name,"user",16); scpy(users[user_count].pass,"1234",16);
    users[user_count].active=1; user_count++;

    current_user[0]='\0';
}

int users_check(const char *name, const char *pass) {
    for (int i=0;i<user_count;i++)
        if (seq(users[i].name,name) && seq(users[i].pass,pass)) return 1;
    return 0;
}

int users_set_password(const char *name, const char *newpass) {
    for (int i=0;i<user_count;i++)
        if (seq(users[i].name,name)) { scpy(users[i].pass,newpass,16); return 1; }
    return 0;
}

void users_set_current(const char *name) { scpy(current_user,name,16); }
const char* users_get_current(void) { return current_user; }
int users_count(void) { return user_count; }
const char* users_name(int i) { return users[i].name; }
