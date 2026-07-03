#ifndef USERS_H
#define USERS_H

void users_init(void);
int  users_check(const char *name, const char *pass);
int  users_set_password(const char *name, const char *newpass);
void users_set_current(const char *name);
const char* users_get_current(void);
int  users_count(void);
const char* users_name(int i);

#endif
