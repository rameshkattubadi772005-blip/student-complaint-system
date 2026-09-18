#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080

#define LOW 1
#define MEDIUM 2
#define HIGH 3

#define STUDENT_ROLE 1
#define ADMIN_ROLE 2

#define DATA_FILE "complaints.dat"
#define ACCOUNT_FILE "student_accounts.dat"
#define MAX_STUDENTS 500

#define REQUEST_SIZE 65536
#define MAX_SESSIONS 100
#define SESSION_TIMEOUT 28800

struct StudentAccount
{
    char studentId[50];
    char password[100];
    char name[100];
    char department[50];
};

struct Complaint
{
    int complaintId;

    char stuID[50];
    char name[100];
    char department[50];

    char complaintType[50];
    char complaint[500];

    int affected;

    int priority;
    int resolutionHours;

    char assignedTo[100];
    char status[30];

    char resolution[500];
    char escalation[30];

    time_t submittedAt;

    char history[2000];

    int inQueue;

    struct Complaint *next;
    struct Complaint *qnext;
};

struct Session
{
    char token[100];

    int role;

    char studentId[50];
    char studentName[100];
    char department[50];

    time_t lastActivity;

    int active;
};

struct Complaint *head = NULL;
struct Complaint *front = NULL;

struct Session sessions[MAX_SESSIONS];

int complaintCount = 0;
unsigned int sessionCounter = 1;
struct StudentAccount studentAccounts[MAX_STUDENTS];
int studentCount = 0;


/* =========================================================
   BASIC INPUT FUNCTIONS
   ========================================================= */

void readLine(char *str, int size)
{
    if (fgets(str, size, stdin) != NULL)
    {
        str[strcspn(str, "\n")] = '\0';
    }
}


/* =========================================================
   PRIORITY FUNCTIONS
   ========================================================= */

int typePriority(const char *type)
{
    if (strcmp(type, "Safety") == 0)
        return HIGH;

    if (strcmp(type, "Health") == 0)
        return HIGH;

    if (strcmp(type, "Academic") == 0)
        return MEDIUM;

    if (strcmp(type, "Infrastructure") == 0)
        return MEDIUM;

    if (strcmp(type, "Administrative") == 0)
        return LOW;

    return LOW;
}


int affectedPriority(int affected)
{
    if (affected >= 50)
        return HIGH;

    if (affected >= 10)
        return MEDIUM;

    return LOW;
}


int calculatePriority(const char *type, int affected)
{
    int p1 = typePriority(type);
    int p2 = affectedPriority(affected);

    return (p1 > p2) ? p1 : p2;
}


int getResolutionTime(int priority)
{
    if (priority == HIGH)
        return 24;

    if (priority == MEDIUM)
        return 72;

    return 168;
}


const char *priorityText(int priority)
{
    if (priority == HIGH)
        return "HIGH";

    if (priority == MEDIUM)
        return "MEDIUM";

    return "LOW";
}


int isValidComplaintType(const char *type)
{
    if (strcmp(type, "Academic") == 0)
        return 1;

    if (strcmp(type, "Infrastructure") == 0)
        return 1;

    if (strcmp(type, "Health") == 0)
        return 1;

    if (strcmp(type, "Safety") == 0)
        return 1;

    if (strcmp(type, "Administrative") == 0)
        return 1;

    return 0;
}


/* =========================================================
   STAFF ASSIGNMENT
   ========================================================= */

void assignMember(struct Complaint *c)
{
    if (strcmp(c->complaintType, "Academic") == 0)
    {
        strcpy(c->assignedTo, "Academic Coordinator");
    }
    else if (strcmp(c->complaintType, "Infrastructure") == 0)
    {
        strcpy(c->assignedTo, "Maintenance Team");
    }
    else if (strcmp(c->complaintType, "Health") == 0)
    {
        strcpy(c->assignedTo, "Health Officer");
    }
    else if (strcmp(c->complaintType, "Safety") == 0)
    {
        strcpy(c->assignedTo, "Safety Officer");
    }
    else
    {
        strcpy(c->assignedTo, "Administrative Officer");
    }
}


/* =========================================================
   HISTORY
   ========================================================= */

void addHistory(struct Complaint *c, const char *message)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char entry[400];

    if (t != NULL)
    {
        snprintf(
            entry,
            sizeof(entry),
            "[%02d-%02d-%04d %02d:%02d] %s\n",
            t->tm_mday,
            t->tm_mon + 1,
            t->tm_year + 1900,
            t->tm_hour,
            t->tm_min,
            message
        );
    }
    else
    {
        snprintf(entry, sizeof(entry), "%s\n", message);
    }

    if (strlen(c->history) + strlen(entry) < sizeof(c->history))
    {
        strcat(c->history, entry);
    }
}


/* =========================================================
   SEARCH / DUPLICATE
   ========================================================= */

struct Complaint *findComplaint(int id)
{
    struct Complaint *current = head;

    while (current != NULL)
    {
        if (current->complaintId == id)
            return current;

        current = current->next;
    }

    return NULL;
}


int isDuplicate(const char *stuID, const char *complaint)
{
    struct Complaint *current = head;

    while (current != NULL)
    {
        if (
            strcmp(current->stuID, stuID) == 0 &&
            strcmp(current->complaint, complaint) == 0
        )
        {
            return 1;
        }

        current = current->next;
    }

    return 0;
}


/* =========================================================
   PRIORITY QUEUE
   ========================================================= */

void enqueue(struct Complaint *c)
{
    c->qnext = NULL;
    c->inQueue = 1;

    if (front == NULL)
    {
        front = c;
        return;
    }

    if (c->priority > front->priority)
    {
        c->qnext = front;
        front = c;
        return;
    }

    struct Complaint *current = front;

    while (
        current->qnext != NULL &&
        current->qnext->priority >= c->priority
    )
    {
        current = current->qnext;
    }

    c->qnext = current->qnext;
    current->qnext = c;
}


struct Complaint *removeFromQueue(void)
{
    if (front == NULL)
        return NULL;

    struct Complaint *temp = front;

    front = front->qnext;

    temp->qnext = NULL;
    temp->inQueue = 0;

    return temp;
}


/* =========================================================
   FILE HANDLING
   ========================================================= */

void saveComplaints(void)
{
    FILE *fp = fopen(DATA_FILE, "wb");

    if (fp == NULL)
    {
        printf("Unable to save complaints.\n");
        return;
    }

    struct Complaint *current = head;

    while (current != NULL)
    {
        fwrite(current, sizeof(struct Complaint), 1, fp);
        current = current->next;
    }

    fclose(fp);
}


void loadComplaints(void)
{
    FILE *fp = fopen(DATA_FILE, "rb");

    if (fp == NULL)
        return;

    struct Complaint temp;

    while (fread(&temp, sizeof(struct Complaint), 1, fp) == 1)
    {
        struct Complaint *newNode =
            (struct Complaint *)malloc(sizeof(struct Complaint));

        if (newNode == NULL)
            break;

        *newNode = temp;

        newNode->next = NULL;
        newNode->qnext = NULL;

        if (head == NULL)
        {
            head = newNode;
        }
        else
        {
            struct Complaint *current = head;

            while (current->next != NULL)
                current = current->next;

            current->next = newNode;
        }

        if (newNode->complaintId > complaintCount)
            complaintCount = newNode->complaintId;
    }

    fclose(fp);

    struct Complaint *current = head;

    while (current != NULL)
    {
        if (
            current->inQueue == 1 &&
            strcmp(current->status, "RESOLVED") != 0
        )
        {
            enqueue(current);
        }
        else
        {
            current->inQueue = 0;
        }

        current = current->next;
    }
}


/* =========================================================
   URL DECODING
   ========================================================= */

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}


void urlDecode(char *str)
{
    char *src = str;
    char *dst = str;

    while (*src)
    {
        if (*src == '%')
        {
            if (
                src[1] != '\0' &&
                src[2] != '\0'
            )
            {
                int h1 = hexValue(src[1]);
                int h2 = hexValue(src[2]);

                if (h1 >= 0 && h2 >= 0)
                {
                    *dst = (char)(h1 * 16 + h2);
                    src += 3;
                    dst++;
                    continue;
                }
            }
        }

        if (*src == '+')
            *dst = ' ';
        else
            *dst = *src;

        src++;
        dst++;
    }

    *dst = '\0';
}


/* =========================================================
   FORM DATA
   ========================================================= */

void getFormValue(
    const char *body,
    const char *key,
    char *output,
    int outputSize
)
{
    output[0] = '\0';

    char search[100];

    snprintf(search, sizeof(search), "%s=", key);

    const char *start = strstr(body, search);

    if (start == NULL)
        return;

    start += strlen(search);

    const char *end = strchr(start, '&');

    int length;

    if (end == NULL)
        length = (int)strlen(start);
    else
        length = (int)(end - start);

    if (length >= outputSize)
        length = outputSize - 1;

    strncpy(output, start, length);

    output[length] = '\0';

    urlDecode(output);
}


/* =========================================================
   HTML ESCAPE
   ========================================================= */

void htmlEscape(
    const char *input,
    char *output,
    int outputSize
)
{
    int pos = 0;

    for (int i = 0; input[i] != '\0'; i++)
    {
        const char *replacement = NULL;

        if (input[i] == '&')
            replacement = "&amp;";
        else if (input[i] == '<')
            replacement = "&lt;";
        else if (input[i] == '>')
            replacement = "&gt;";
        else if (input[i] == '"')
            replacement = "&quot;";
        else if (input[i] == '\'')
            replacement = "&#39;";

        if (replacement != NULL)
        {
            int len = (int)strlen(replacement);

            if (pos + len >= outputSize - 1)
                break;

            strcpy(output + pos, replacement);
            pos += len;
        }
        else
        {
            if (pos >= outputSize - 1)
                break;

            output[pos++] = input[i];
        }
    }

    output[pos] = '\0';
}


void historyForHTML(
    const char *input,
    char *output,
    int outputSize
)
{
    htmlEscape(input, output, outputSize);

    for (int i = 0; output[i] != '\0'; i++)
    {
        if (output[i] == '\n')
        {
            output[i] = ' ';
        }
    }
}


/* =========================================================
   SOCKET FUNCTIONS
   ========================================================= */

int sendAll(
    SOCKET client,
    const char *data,
    int length
)
{
    int total = 0;

    while (total < length)
    {
        int sent = send(
            client,
            data + total,
            length - total,
            0
        );

        if (sent == SOCKET_ERROR)
            return 0;

        total += sent;
    }

    return 1;
}


void sendResponse(
    SOCKET client,
    const char *html
)
{
    char header[512];

    int length = (int)strlen(html);

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        length
    );

    sendAll(client, header, (int)strlen(header));
    sendAll(client, html, length);
}


void sendRedirect(
    SOCKET client,
    const char *location
)
{
    char response[1000];

    snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        location
    );

    sendAll(client, response, (int)strlen(response));
}


void sendRedirectWithCookie(
    SOCKET client,
    const char *location,
    const char *token
)
{
    char response[1500];

    snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Set-Cookie: session=%s; Path=/; HttpOnly; SameSite=Lax\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        location,
        token
    );

    sendAll(client, response, (int)strlen(response));
}


void sendLogoutResponse(SOCKET client)
{
    const char *response =
        "HTTP/1.1 302 Found\r\n"
        "Location: /login\r\n"
        "Set-Cookie: session=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    sendAll(
        client,
        response,
        (int)strlen(response)
    );
}


void sendError(
    SOCKET client,
    const char *message
)
{
    char safe[1000];

    htmlEscape(
        message,
        safe,
        sizeof(safe)
    );

    char html[4000];

    snprintf(
        html,
        sizeof(html),

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<title>Error</title>"
        "<style>"
        "body{"
        "font-family:Arial,sans-serif;"
        "background:#f4f7fb;"
        "display:flex;"
        "justify-content:center;"
        "align-items:center;"
        "min-height:100vh;"
        "margin:0;"
        "}"
        ".box{"
        "background:white;"
        "padding:40px;"
        "border-radius:16px;"
        "box-shadow:0 10px 30px rgba(0,0,0,.08);"
        "text-align:center;"
        "max-width:500px;"
        "}"
        "a{"
        "display:inline-block;"
        "margin-top:20px;"
        "padding:12px 22px;"
        "background:#2563eb;"
        "color:white;"
        "text-decoration:none;"
        "border-radius:8px;"
        "}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='box'>"
        "<h2>Something went wrong</h2>"
        "<p>%s</p>"
        "<a href='/'>Go Home</a>"
        "</div>"
        "</body>"
        "</html>",

        safe
    );

    sendResponse(client, html);
}


/* =========================================================
   SERVE HTML FILE
   ========================================================= */

void serveFile(
    SOCKET client,
    const char *filename
)
{
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL)
    {
        sendError(client, "HTML file not found.");
        return;
    }

    fseek(fp, 0, SEEK_END);

    long size = ftell(fp);

    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > 60000)
    {
        fclose(fp);
        sendError(client, "Invalid HTML file.");
        return;
    }

    char *content =
        (char *)malloc((size_t)size + 1);

    if (content == NULL)
    {
        fclose(fp);
        sendError(client, "Memory allocation failed.");
        return;
    }

    fread(content, 1, size, fp);

    content[size] = '\0';

    fclose(fp);

    sendResponse(client, content);

    free(content);
}


/* =========================================================
   SESSION SYSTEM
   ========================================================= */

void generateSessionToken(
    char *token,
    int size
)
{
    snprintf(
        token,
        size,
        "%08X%08X%08X%08X",
        (unsigned int)time(NULL),
        (unsigned int)rand(),
        sessionCounter++,
        (unsigned int)GetTickCount()
    );
}


int createSession(
    int role,
    const char *studentId,
    const char *studentName,
    const char *department
)
{
    time_t now = time(NULL);

    int slot = -1;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!sessions[i].active)
        {
            slot = i;
            break;
        }

        if (
            difftime(
                now,
                sessions[i].lastActivity
            ) > SESSION_TIMEOUT
        )
        {
            sessions[i].active = 0;
            slot = i;
            break;
        }
    }

    if (slot == -1)
        return -1;

    memset(
        &sessions[slot],
        0,
        sizeof(struct Session)
    );

    generateSessionToken(
        sessions[slot].token,
        sizeof(sessions[slot].token)
    );

    sessions[slot].role = role;

    strcpy(
        sessions[slot].studentId,
        studentId
    );

    strcpy(
        sessions[slot].studentName,
        studentName
    );

    strcpy(
        sessions[slot].department,
        department
    );

    sessions[slot].lastActivity = now;

    sessions[slot].active = 1;

    return slot;
}


int getSessionTokenFromRequest(
    const char *request,
    char *token,
    int size
)
{
    token[0] = '\0';

    const char *cookie =
        strstr(request, "Cookie:");

    if (cookie == NULL)
        return 0;

    const char *p =
        strstr(cookie, "session=");

    if (p == NULL)
        return 0;

    p += strlen("session=");

    const char *end =
        strchr(p, ';');

    if (end == NULL)
        end = strstr(p, "\r\n");

    int length;

    if (end == NULL)
        length = (int)strlen(p);
    else
        length = (int)(end - p);

    if (length <= 0)
        return 0;

    if (length >= size)
        length = size - 1;

    strncpy(token, p, length);

    token[length] = '\0';

    return 1;
}


int findSession(
    const char *request
)
{
    char token[100];

    if (
        !getSessionTokenFromRequest(
            request,
            token,
            sizeof(token)
        )
    )
    {
        return -1;
    }

    time_t now = time(NULL);

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (
            sessions[i].active &&
            strcmp(
                sessions[i].token,
                token
            ) == 0
        )
        {
            if (
                difftime(
                    now,
                    sessions[i].lastActivity
                ) > SESSION_TIMEOUT
            )
            {
                sessions[i].active = 0;
                return -1;
            }

            sessions[i].lastActivity = now;

            return i;
        }
    }

    return -1;
}


/* =========================================================
   STUDENT ACCOUNT SYSTEM
   ========================================================= */

int findStudentAccount(const char *studentId)
{
    for (int i = 0; i < studentCount; i++)
    {
        if (strcmp(studentAccounts[i].studentId, studentId) == 0)
            return i;
    }

    return -1;
}

void saveStudentAccounts(void)
{
    FILE *fp = fopen(ACCOUNT_FILE, "wb");

    if (fp == NULL)
    {
        printf("Unable to save student accounts.\n");
        return;
    }

    fwrite(
        studentAccounts,
        sizeof(struct StudentAccount),
        studentCount,
        fp
    );

    fclose(fp);
}

void loadStudentAccounts(void)
{
    FILE *fp = fopen(ACCOUNT_FILE, "rb");

    if (fp != NULL)
    {
        studentCount = (int)fread(
            studentAccounts,
            sizeof(struct StudentAccount),
            MAX_STUDENTS,
            fp
        );

        fclose(fp);
    }

    /* Create the original demo students if the account file is new. */
    if (studentCount == 0)
    {
        strcpy(studentAccounts[0].studentId, "STU001");
        strcpy(studentAccounts[0].password, "student123");
        strcpy(studentAccounts[0].name, "Student One");
        strcpy(studentAccounts[0].department, "CSM");

        strcpy(studentAccounts[1].studentId, "STU002");
        strcpy(studentAccounts[1].password, "student123");
        strcpy(studentAccounts[1].name, "Student Two");
        strcpy(studentAccounts[1].department, "CSE");

        studentCount = 2;
        saveStudentAccounts();
    }
}

int registerStudent(
    const char *studentId,
    const char *password,
    const char *name,
    const char *department
)
{
    if (studentCount >= MAX_STUDENTS)
        return -1;

    if (findStudentAccount(studentId) != -1)
        return 0;

    strcpy(studentAccounts[studentCount].studentId, studentId);
    strcpy(studentAccounts[studentCount].password, password);
    strcpy(studentAccounts[studentCount].name, name);
    strcpy(studentAccounts[studentCount].department, department);

    studentCount++;
    saveStudentAccounts();

    return 1;
}

void serveRegisterPage(SOCKET client)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Create Student Account</title>"
        "<style>"
        "body{margin:0;font-family:Arial,sans-serif;background:#f4f7fb;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;box-sizing:border-box}"
        ".box{background:#fff;width:100%;max-width:520px;padding:35px;border-radius:18px;box-shadow:0 10px 30px rgba(0,0,0,.08)}"
        "h1{margin-top:0;color:#1e3a8a;text-align:center}"
        ".sub{text-align:center;color:#64748b;margin-bottom:25px}"
        "label{display:block;margin:15px 0 7px;font-weight:bold;color:#334155}"
        "input{width:100%;box-sizing:border-box;padding:13px;border:1px solid #cbd5e1;border-radius:9px;font-size:15px}"
        "input:focus{outline:none;border-color:#2563eb}"
        "button{width:100%;margin-top:25px;padding:13px;border:0;border-radius:9px;background:#2563eb;color:white;font-size:16px;font-weight:bold;cursor:pointer}"
        ".login{text-align:center;margin-top:20px;color:#64748b}.login a{color:#2563eb;text-decoration:none;font-weight:bold}"
        "</style></head>"
        "<body><div class='box'>"
        "<h1>Create Student Account</h1>"
        "<p class='sub'>Register for the StudentCare complaint portal</p>"
        "<form action='/register' method='POST'>"
        "<label>Student ID</label>"
        "<input type='text' name='studentId' placeholder='Example: STU003' maxlength='49' required>"
        "<label>Full Name</label>"
        "<input type='text' name='name' placeholder='Enter your full name' maxlength='99' required>"
        "<label>Department</label>"
        "<input type='text' name='department' placeholder='Example: CSM' maxlength='49' required>"
        "<label>Password</label>"
        "<input type='password' name='password' placeholder='Minimum 6 characters' maxlength='99' required>"
        "<label>Confirm Password</label>"
        "<input type='password' name='confirmPassword' placeholder='Re-enter your password' maxlength='99' required>"
        "<button type='submit'>Create Account</button>"
        "</form>"
        "<div class='login'>Already have an account? <a href='/login'>Login here</a></div>"
        "</div></body></html>";

    sendResponse(client, html);
}

void handleRegister(SOCKET client, const char *body)
{
    char studentId[50];
    char name[100];
    char department[50];
    char password[100];
    char confirmPassword[100];

    getFormValue(body, "studentId", studentId, sizeof(studentId));
    getFormValue(body, "name", name, sizeof(name));
    getFormValue(body, "department", department, sizeof(department));
    getFormValue(body, "password", password, sizeof(password));
    getFormValue(body, "confirmPassword", confirmPassword, sizeof(confirmPassword));

    if (strlen(studentId) == 0 || strlen(name) == 0 ||
        strlen(department) == 0 || strlen(password) == 0 ||
        strlen(confirmPassword) == 0)
    {
        sendError(client, "All registration fields are required.");
        return;
    }

    if (strlen(password) < 6)
    {
        sendError(client, "Password must contain at least 6 characters.");
        return;
    }

    if (strcmp(password, confirmPassword) != 0)
    {
        sendError(client, "Password and confirm password do not match.");
        return;
    }

    if (findStudentAccount(studentId) != -1)
    {
        sendError(client, "Student ID already exists. Please use a different Student ID.");
        return;
    }

    if (studentCount >= MAX_STUDENTS)
    {
        sendError(client, "Student account limit reached.");
        return;
    }

    int result = registerStudent(studentId, password, name, department);

    if (result != 1)
    {
        sendError(client, "Unable to create the student account.");
        return;
    }

    char safeId[120];
    htmlEscape(studentId, safeId, sizeof(safeId));

    char success[4000];
    snprintf(
        success,
        sizeof(success),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>Registration Successful</title><style>body{margin:0;font-family:Arial,sans-serif;background:#f4f7fb;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}.box{background:white;max-width:550px;width:100%%;padding:40px;border-radius:18px;box-shadow:0 10px 30px rgba(0,0,0,.08);text-align:center}h1{color:#15803d}.id{background:#f0fdf4;padding:15px;border-radius:10px;font-size:20px;font-weight:bold;margin:20px 0}a{display:inline-block;padding:12px 22px;background:#2563eb;color:white;text-decoration:none;border-radius:8px}</style></head><body><div class='box'><h1>Account Created Successfully!</h1><p>Your StudentCare account has been created.</p><div class='id'>Student ID: %s</div><a href='/login'>Go to Login</a></div></body></html>",
        safeId
    );

    sendResponse(client, success);
}

/* =========================================================
   LOGIN VALIDATION
   ========================================================= */

int validateStudent(
    const char *username,
    const char *password,
    char *name,
    char *department
)
{
    int index = findStudentAccount(username);

    if (index == -1)
        return 0;

    if (strcmp(studentAccounts[index].password, password) != 0)
        return 0;

    strcpy(name, studentAccounts[index].name);
    strcpy(department, studentAccounts[index].department);

    return 1;
}

int validateAdmin(
    const char *username,
    const char *password
)
{
    if (
        strcmp(username, "admin") == 0 &&
        strcmp(password, "admin123") == 0
    )
    {
        return 1;
    }

    return 0;
}


/* =========================================================
   LOGIN
   ========================================================= */

void handleLogin(
    SOCKET client,
    const char *body
)
{
    char role[30];
    char username[100];
    char password[100];

    getFormValue(
        body,
        "role",
        role,
        sizeof(role)
    );

    getFormValue(
        body,
        "username",
        username,
        sizeof(username)
    );

    getFormValue(
        body,
        "password",
        password,
        sizeof(password)
    );

    if (
        strlen(role) == 0 ||
        strlen(username) == 0 ||
        strlen(password) == 0
    )
    {
        sendError(
            client,
            "Please enter username and password."
        );

        return;
    }


    /* ADMIN LOGIN */

    if (strcmp(role, "admin") == 0)
    {
        if (
            validateAdmin(
                username,
                password
            )
        )
        {
            int index =
                createSession(
                    ADMIN_ROLE,
                    "",
                    "",
                    ""
                );

            if (index == -1)
            {
                sendError(
                    client,
                    "Maximum active sessions reached."
                );

                return;
            }

            sendRedirectWithCookie(
                client,
                "/admin",
                sessions[index].token
            );

            return;
        }
    }


    /* STUDENT LOGIN */

    if (strcmp(role, "student") == 0)
    {
        char name[100];
        char department[50];

        if (
            validateStudent(
                username,
                password,
                name,
                department
            )
        )
        {
            int index =
                createSession(
                    STUDENT_ROLE,
                    username,
                    name,
                    department
                );

            if (index == -1)
            {
                sendError(
                    client,
                    "Maximum active sessions reached."
                );

                return;
            }

            sendRedirectWithCookie(
                client,
                "/",
                sessions[index].token
            );

            return;
        }
    }

    sendError(
        client,
        "Invalid username, password, or account type."
    );
}


/* =========================================================
   AUTHORIZATION
   ========================================================= */

int requireRole(
    SOCKET client,
    int sessionIndex,
    int requiredRole
)
{
    if (sessionIndex < 0)
    {
        sendRedirect(client, "/login");
        return 0;
    }

    if (
        sessions[sessionIndex].role != requiredRole
    )
    {
        sendError(
            client,
            "You do not have permission to access this page."
        );

        return 0;
    }

    return 1;
}


/* =========================================================
   STUDENT HOME
   ========================================================= */

void serveIndex(
    SOCKET client,
    struct Session *session
)
{
    char safeName[200];
    char safeId[100];
    char safeDepartment[100];

    htmlEscape(
        session->studentName,
        safeName,
        sizeof(safeName)
    );

    htmlEscape(
        session->studentId,
        safeId,
        sizeof(safeId)
    );

    htmlEscape(
        session->department,
        safeDepartment,
        sizeof(safeDepartment)
    );

    char html[12000];

    snprintf(
        html,
        sizeof(html),

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Student Portal</title>"

        "<style>"

        "*{box-sizing:border-box;}"

        "body{"
        "margin:0;"
        "font-family:Arial,sans-serif;"
        "background:#f4f7fb;"
        "color:#1f2937;"
        "}"

        "header{"
        "background:#2563eb;"
        "color:white;"
        "padding:20px 7%;"
        "display:flex;"
        "justify-content:space-between;"
        "align-items:center;"
        "}"

        ".logo{"
        "font-size:24px;"
        "font-weight:bold;"
        "}"

        ".logout{"
        "color:white;"
        "text-decoration:none;"
        "border:1px solid rgba(255,255,255,.6);"
        "padding:9px 16px;"
        "border-radius:8px;"
        "}"

        ".container{"
        "max-width:1000px;"
        "margin:50px auto;"
        "padding:0 20px;"
        "}"

        ".welcome{"
        "background:white;"
        "padding:30px;"
        "border-radius:18px;"
        "box-shadow:0 8px 25px rgba(0,0,0,.06);"
        "margin-bottom:25px;"
        "}"

        ".welcome h1{"
        "margin-top:0;"
        "}"

        ".profile{"
        "display:grid;"
        "grid-template-columns:repeat(3,1fr);"
        "gap:15px;"
        "margin-top:25px;"
        "}"

        ".info{"
        "background:#f8fafc;"
        "padding:18px;"
        "border-radius:12px;"
        "}"

        ".label{"
        "font-size:13px;"
        "color:#64748b;"
        "margin-bottom:6px;"
        "}"

        ".value{"
        "font-size:17px;"
        "font-weight:bold;"
        "}"

        ".actions{"
        "display:grid;"
        "grid-template-columns:repeat(2,1fr);"
        "gap:20px;"
        "}"

        ".card{"
        "background:white;"
        "padding:30px;"
        "border-radius:18px;"
        "box-shadow:0 8px 25px rgba(0,0,0,.06);"
        "text-decoration:none;"
        "color:#1f2937;"
        "transition:.2s;"
        "}"

        ".card:hover{"
        "transform:translateY(-3px);"
        "}"

        ".card h2{"
        "margin-top:0;"
        "color:#2563eb;"
        "}"

        ".button{"
        "display:inline-block;"
        "margin-top:15px;"
        "padding:12px 20px;"
        "background:#2563eb;"
        "color:white;"
        "border-radius:8px;"
        "text-decoration:none;"
        "}"

        "@media(max-width:700px){"
        ".profile,.actions{grid-template-columns:1fr;}"
        "}"

        "</style>"
        "</head>"

        "<body>"

        "<header>"
        "<div class='logo'>StudentCare</div>"
        "<a class='logout' href='/logout'>Logout</a>"
        "</header>"

        "<div class='container'>"

        "<div class='welcome'>"
        "<h1>Welcome, %s</h1>"
        "<p>Student Portal</p>"

        "<div class='profile'>"

        "<div class='info'>"
        "<div class='label'>Student ID</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='info'>"
        "<div class='label'>Department</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='info'>"
        "<div class='label'>Account Type</div>"
        "<div class='value'>Student</div>"
        "</div>"

        "</div>"
        "</div>"

        "<div class='actions'>"

        "<a class='card' href='/submit'>"
        "<h2>Submit a Complaint</h2>"
        "<p>Report an academic, infrastructure, health, safety, or administrative issue.</p>"
        "<span class='button'>Submit Complaint</span>"
        "</a>"

        "<a class='card' href='/track'>"
        "<h2>Track Complaint</h2>"
        "<p>Check the status, priority, assigned staff, and history of your complaint.</p>"
        "<span class='button'>Track Complaint</span>"
        "</a>"

        "</div>"

        "</div>"

        "</body>"
        "</html>",

        safeName,
        safeId,
        safeDepartment
    );

    sendResponse(client, html);
}


/* =========================================================
   COMPLAINT SUCCESS PAGE
   ========================================================= */

void showComplaintSuccess(
    SOCKET client,
    struct Complaint *c
)
{
    char safeComplaint[1200];
    char safeType[200];
    char safeAssigned[200];

    htmlEscape(
        c->complaint,
        safeComplaint,
        sizeof(safeComplaint)
    );

    htmlEscape(
        c->complaintType,
        safeType,
        sizeof(safeType)
    );

    htmlEscape(
        c->assignedTo,
        safeAssigned,
        sizeof(safeAssigned)
    );

    char html[9000];

    snprintf(
        html,
        sizeof(html),

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Complaint Submitted</title>"

        "<style>"
        "body{"
        "font-family:Arial,sans-serif;"
        "background:#f4f7fb;"
        "margin:0;"
        "padding:50px 20px;"
        "}"

        ".box{"
        "max-width:700px;"
        "margin:auto;"
        "background:white;"
        "padding:35px;"
        "border-radius:18px;"
        "box-shadow:0 10px 30px rgba(0,0,0,.08);"
        "}"

        ".success{"
        "color:#15803d;"
        "font-size:20px;"
        "font-weight:bold;"
        "}"

        ".row{"
        "padding:14px 0;"
        "border-bottom:1px solid #e5e7eb;"
        "}"

        ".label{"
        "color:#64748b;"
        "font-size:13px;"
        "}"

        ".value{"
        "font-size:17px;"
        "margin-top:5px;"
        "}"

        ".button{"
        "display:inline-block;"
        "margin-top:20px;"
        "padding:12px 20px;"
        "background:#2563eb;"
        "color:white;"
        "text-decoration:none;"
        "border-radius:8px;"
        "}"

        "</style>"
        "</head>"

        "<body>"

        "<div class='box'>"

        "<div class='success'>Complaint submitted successfully!</div>"

        "<div class='row'>"
        "<div class='label'>Complaint ID</div>"
        "<div class='value'>C%d</div>"
        "</div>"

        "<div class='row'>"
        "<div class='label'>Complaint Type</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='row'>"
        "<div class='label'>Complaint</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='row'>"
        "<div class='label'>Priority</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='row'>"
        "<div class='label'>Resolution Time</div>"
        "<div class='value'>%d hours</div>"
        "</div>"

        "<div class='row'>"
        "<div class='label'>Assigned To</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<a class='button' href='/'>Back to Student Portal</a>"

        "</div>"

        "</body>"
        "</html>",

        c->complaintId,
        safeType,
        safeComplaint,
        priorityText(c->priority),
        c->resolutionHours,
        safeAssigned
    );

    sendResponse(client, html);
}


/* =========================================================
   SUBMIT COMPLAINT
   ========================================================= */

void handleSubmit(
    SOCKET client,
    const char *body,
    struct Session *session
)
{
    char complaintType[50];
    char complaint[500];
    char affectedText[50];

    getFormValue(
        body,
        "complaintType",
        complaintType,
        sizeof(complaintType)
    );

    getFormValue(
        body,
        "complaint",
        complaint,
        sizeof(complaint)
    );

    getFormValue(
        body,
        "affected",
        affectedText,
        sizeof(affectedText)
    );

    if (
        strlen(complaintType) == 0 ||
        strlen(complaint) == 0 ||
        strlen(affectedText) == 0
    )
    {
        sendError(
            client,
            "Please fill all complaint fields."
        );

        return;
    }

    if (!isValidComplaintType(complaintType))
    {
        sendError(
            client,
            "Invalid complaint type."
        );

        return;
    }

    int affected = atoi(affectedText);

    if (affected < 1)
    {
        sendError(
            client,
            "Affected number must be greater than zero."
        );

        return;
    }


    /* DUPLICATE CHECK */

    if (
        isDuplicate(
            session->studentId,
            complaint
        )
    )
    {
        sendError(
            client,
            "You have already submitted the same complaint."
        );

        return;
    }


    struct Complaint *newNode =
        (struct Complaint *)malloc(
            sizeof(struct Complaint)
        );

    if (newNode == NULL)
    {
        sendError(
            client,
            "Memory allocation failed."
        );

        return;
    }

    memset(
        newNode,
        0,
        sizeof(struct Complaint)
    );

    complaintCount++;

    newNode->complaintId =
        complaintCount;

    strcpy(
        newNode->stuID,
        session->studentId
    );

    strcpy(
        newNode->name,
        session->studentName
    );

    strcpy(
        newNode->department,
        session->department
    );

    strcpy(
        newNode->complaintType,
        complaintType
    );

    strcpy(
        newNode->complaint,
        complaint
    );

    newNode->affected = affected;

    newNode->priority =
        calculatePriority(
            complaintType,
            affected
        );

    newNode->resolutionHours =
        getResolutionTime(
            newNode->priority
        );

    assignMember(newNode);

    strcpy(
        newNode->status,
        "NEW"
    );

    strcpy(
        newNode->resolution,
        ""
    );

    strcpy(
        newNode->escalation,
        "NO"
    );

    newNode->submittedAt =
        time(NULL);

    newNode->history[0] = '\0';

    newNode->inQueue = 0;

    newNode->next = NULL;
    newNode->qnext = NULL;


    /* HISTORY */

    addHistory(
        newNode,
        "Complaint submitted."
    );


    /* MAIN LINKED LIST */

    if (head == NULL)
    {
        head = newNode;
    }
    else
    {
        struct Complaint *current = head;

        while (current->next != NULL)
            current = current->next;

        current->next = newNode;
    }


    /* PRIORITY QUEUE */

    enqueue(newNode);


    /* SAVE */

    saveComplaints();


    showComplaintSuccess(
        client,
        newNode
    );
}


/* =========================================================
   ESCALATION
   ========================================================= */

void checkEscalation(void)
{
    time_t now = time(NULL);

    struct Complaint *current = head;

    while (current != NULL)
    {
        if (
            strcmp(
                current->status,
                "RESOLVED"
            ) != 0
        )
        {
            double hoursPassed =
                difftime(
                    now,
                    current->submittedAt
                ) / 3600.0;

            if (
                hoursPassed >=
                current->resolutionHours
            )
            {
                if (
                    strcmp(
                        current->escalation,
                        "YES"
                    ) != 0
                )
                {
                    strcpy(
                        current->escalation,
                        "YES"
                    );

                    char message[400];

                    snprintf(
                        message,
                        sizeof(message),
                        "Complaint C%d escalated because the resolution time was exceeded.",
                        current->complaintId
                    );

                    addHistory(
                        current,
                        message
                    );
                }
            }
        }

        current = current->next;
    }

    saveComplaints();
}


/* =========================================================
   ADMIN DASHBOARD
   ========================================================= */

void adminDashboard(SOCKET client)
{
    checkEscalation();

    int total = 0;
    int newCount = 0;
    int assignedCount = 0;
    int progressCount = 0;
    int resolvedCount = 0;
    int escalatedCount = 0;

    struct Complaint *current = head;

    while (current != NULL)
    {
        total++;

        if (
            strcmp(
                current->status,
                "NEW"
            ) == 0
        )
            newCount++;

        if (
            strcmp(
                current->status,
                "ASSIGNED"
            ) == 0
        )
            assignedCount++;

        if (
            strcmp(
                current->status,
                "IN PROGRESS"
            ) == 0
        )
            progressCount++;

        if (
            strcmp(
                current->status,
                "RESOLVED"
            ) == 0
        )
            resolvedCount++;

        if (
            strcmp(
                current->escalation,
                "YES"
            ) == 0
        )
            escalatedCount++;

        current = current->next;
    }


    char rows[40000];

    rows[0] = '\0';

    current = head;

    while (current != NULL)
    {
        char safeName[200];
        char safeType[150];
        char safePriority[50];
        char safeAssigned[200];
        char safeStatus[100];
        char safeEscalation[50];

        htmlEscape(
            current->name,
            safeName,
            sizeof(safeName)
        );

        htmlEscape(
            current->complaintType,
            safeType,
            sizeof(safeType)
        );

        htmlEscape(
            priorityText(current->priority),
            safePriority,
            sizeof(safePriority)
        );

        htmlEscape(
            current->assignedTo,
            safeAssigned,
            sizeof(safeAssigned)
        );

        htmlEscape(
            current->status,
            safeStatus,
            sizeof(safeStatus)
        );

        htmlEscape(
            current->escalation,
            safeEscalation,
            sizeof(safeEscalation)
        );


        char row[3000];

        snprintf(
            row,
            sizeof(row),

            "<tr>"

            "<td>C%d</td>"

            "<td>%s</td>"

            "<td>%s</td>"

            "<td>%d</td>"

            "<td><b>%s</b></td>"

            "<td>%d hrs</td>"

            "<td>%s</td>"

            "<td>%s</td>"

            "<td>%s</td>"

            "<td>%s</td>"

            "<td>"

            "<a class='smallBtn' href='/process'>Process</a>"

            "</td>"

            "</tr>",

            current->complaintId,
            safeName,
            safeType,
            current->affected,
            safePriority,
            current->resolutionHours,
            safeAssigned,
            safeStatus,
            safeEscalation,
            ""
        );

        if (
            strlen(rows) +
            strlen(row) <
            sizeof(rows)
        )
        {
            strcat(rows, row);
        }

        current = current->next;
    }


    char html[55000];

    snprintf(
        html,
        sizeof(html),

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Trainer Dashboard</title>"

        "<style>"

        "*{box-sizing:border-box;}"

        "body{"
        "margin:0;"
        "font-family:Arial,sans-serif;"
        "background:#f4f7fb;"
        "color:#1f2937;"
        "}"

        "header{"
        "background:#111827;"
        "color:white;"
        "padding:20px 5%;"
        "display:flex;"
        "justify-content:space-between;"
        "align-items:center;"
        "}"

        ".title{"
        "font-size:24px;"
        "font-weight:bold;"
        "}"

        ".logout{"
        "color:white;"
        "text-decoration:none;"
        "padding:9px 16px;"
        "border:1px solid #64748b;"
        "border-radius:8px;"
        "}"

        ".container{"
        "padding:30px 5%;"
        "}"

        ".stats{"
        "display:grid;"
        "grid-template-columns:repeat(6,1fr);"
        "gap:15px;"
        "margin-bottom:30px;"
        "}"

        ".stat{"
        "background:white;"
        "padding:20px;"
        "border-radius:14px;"
        "box-shadow:0 5px 18px rgba(0,0,0,.06);"
        "}"

        ".stat h3{"
        "margin:0;"
        "font-size:14px;"
        "color:#64748b;"
        "}"

        ".number{"
        "font-size:28px;"
        "font-weight:bold;"
        "margin-top:8px;"
        "}"

        ".actions{"
        "padding:20px 0;"
        "display:flex;"
        "gap:12px;"
        "flex-wrap:wrap;"
        "}"

        ".button{"
        "padding:12px 18px;"
        "background:#2563eb;"
        "color:white;"
        "text-decoration:none;"
        "border-radius:8px;"
        "}"

        ".tableBox{"
        "background:white;"
        "border-radius:16px;"
        "padding:20px;"
        "overflow:auto;"
        "box-shadow:0 5px 18px rgba(0,0,0,.06);"
        "}"

        "table{"
        "width:100%;"
        "border-collapse:collapse;"
        "min-width:1100px;"
        "}"

        "th,td{"
        "padding:13px;"
        "border-bottom:1px solid #e5e7eb;"
        "text-align:left;"
        "}"

        "th{"
        "background:#f8fafc;"
        "}"

        ".smallBtn{"
        "background:#2563eb;"
        "color:white;"
        "padding:7px 12px;"
        "border-radius:6px;"
        "text-decoration:none;"
        "}"

        "@media(max-width:1000px){"
        ".stats{grid-template-columns:repeat(3,1fr);}"
        "}"

        "</style>"
        "</head>"

        "<body>"

        "<header>"
        "<div class='title'>StudentCare — Trainer Dashboard</div>"
        "<a class='logout' href='/logout'>Logout</a>"
        "</header>"

        "<div class='container'>"

        "<div class='stats'>"

        "<div class='stat'>"
        "<h3>Total Complaints</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "<div class='stat'>"
        "<h3>New</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "<div class='stat'>"
        "<h3>Assigned</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "<div class='stat'>"
        "<h3>In Progress</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "<div class='stat'>"
        "<h3>Resolved</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "<div class='stat'>"
        "<h3>Escalated</h3>"
        "<div class='number'>%d</div>"
        "</div>"

        "</div>"

        "<div class='actions'>"

        "<a class='button' href='/process'>Process Next Complaint</a>"

        "<a class='button' href='/escalation'>Run Escalation Check</a>"

        "<a class='button' href='/admin'>Refresh</a>"

        "</div>"

        "<div class='tableBox'>"

        "<h2>Complaint Records</h2>"

        "<table>"

        "<tr>"
        "<th>ID</th>"
        "<th>Student</th>"
        "<th>Type</th>"
        "<th>Affected</th>"
        "<th>Priority</th>"
        "<th>Resolution Time</th>"
        "<th>Assigned To</th>"
        "<th>Status</th>"
        "<th>Escalation</th>"
        "<th>Action</th>"
        "</tr>"

        "%s"

        "</table>"

        "</div>"

        "</div>"

        "</body>"
        "</html>",

        total,
        newCount,
        assignedCount,
        progressCount,
        resolvedCount,
        escalatedCount,
        rows
    );

    sendResponse(client, html);
}


/* =========================================================
   PROCESS NEXT COMPLAINT
   ========================================================= */

void processNextComplaint(SOCKET client)
{
    struct Complaint *c =
        removeFromQueue();

    if (c == NULL)
    {
        sendError(
            client,
            "No complaints are waiting in the priority queue."
        );

        return;
    }

    strcpy(
        c->status,
        "IN PROGRESS"
    );

    char message[300];

    snprintf(
        message,
        sizeof(message),
        "Complaint C%d moved to IN PROGRESS.",
        c->complaintId
    );

    addHistory(
        c,
        message
    );

    saveComplaints();

    adminDashboard(client);
}


/* =========================================================
   UPDATE COMPLAINT
   ========================================================= */

void updateComplaint(
    SOCKET client,
    const char *body
)
{
    char idText[50];
    char status[50];
    char resolution[500];

    getFormValue(
        body,
        "complaintId",
        idText,
        sizeof(idText)
    );

    getFormValue(
        body,
        "status",
        status,
        sizeof(status)
    );

    getFormValue(
        body,
        "resolution",
        resolution,
        sizeof(resolution)
    );

    if (
        idText[0] == 'C' ||
        idText[0] == 'c'
    )
    {
        memmove(
            idText,
            idText + 1,
            strlen(idText)
        );
    }

    int id = atoi(idText);

    struct Complaint *c =
        findComplaint(id);

    if (c == NULL)
    {
        sendError(
            client,
            "Complaint not found."
        );

        return;
    }

    if (
        strcmp(status, "NEW") != 0 &&
        strcmp(status, "ASSIGNED") != 0 &&
        strcmp(status, "IN PROGRESS") != 0 &&
        strcmp(status, "RESOLVED") != 0
    )
    {
        sendError(
            client,
            "Invalid status."
        );

        return;
    }

    strcpy(
        c->status,
        status
    );

    if (strlen(resolution) > 0)
    {
        strcpy(
            c->resolution,
            resolution
        );
    }

    char message[500];

    snprintf(
        message,
        sizeof(message),
        "Complaint status changed to %s.",
        status
    );

    addHistory(
        c,
        message
    );

    if (
        strcmp(status, "RESOLVED") == 0
    )
    {
        c->inQueue = 0;

        c->qnext = NULL;

        addHistory(
            c,
            "Complaint resolved."
        );
    }

    saveComplaints();

    adminDashboard(client);
}


/* =========================================================
   ESCALATION ROUTE
   ========================================================= */

void handleEscalationRoute(
    SOCKET client
)
{
    checkEscalation();

    adminDashboard(client);
}


/* =========================================================
   TRACK COMPLAINT
   ========================================================= */

void showTrackedComplaint(
    SOCKET client,
    const char *body,
    struct Session *session
)
{
    char idText[50];

    getFormValue(
        body,
        "complaintId",
        idText,
        sizeof(idText)
    );

    if (
        idText[0] == 'C' ||
        idText[0] == 'c'
    )
    {
        memmove(
            idText,
            idText + 1,
            strlen(idText)
        );
    }

    int id = atoi(idText);

    struct Complaint *c =
        findComplaint(id);

    if (c == NULL)
    {
        sendError(
            client,
            "Complaint not found."
        );

        return;
    }


    /* STUDENT CAN SEE ONLY OWN COMPLAINT */

    if (
        strcmp(
            c->stuID,
            session->studentId
        ) != 0
    )
    {
        sendError(
            client,
            "You can only track your own complaints."
        );

        return;
    }


    char safeType[200];
    char safeComplaint[1200];
    char safeAssigned[200];
    char safeStatus[100];
    char safeResolution[1200];
    char safeEscalation[100];
    char safeHistory[5000];

    htmlEscape(
        c->complaintType,
        safeType,
        sizeof(safeType)
    );

    htmlEscape(
        c->complaint,
        safeComplaint,
        sizeof(safeComplaint)
    );

    htmlEscape(
        c->assignedTo,
        safeAssigned,
        sizeof(safeAssigned)
    );

    htmlEscape(
        c->status,
        safeStatus,
        sizeof(safeStatus)
    );

    htmlEscape(
        c->resolution,
        safeResolution,
        sizeof(safeResolution)
    );

    htmlEscape(
        c->escalation,
        safeEscalation,
        sizeof(safeEscalation)
    );

    historyForHTML(
        c->history,
        safeHistory,
        sizeof(safeHistory)
    );


    char html[15000];

    snprintf(
        html,
        sizeof(html),

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Complaint Tracking</title>"

        "<style>"

        "body{"
        "font-family:Arial,sans-serif;"
        "background:#f4f7fb;"
        "margin:0;"
        "padding:40px 20px;"
        "}"

        ".box{"
        "max-width:900px;"
        "margin:auto;"
        "background:white;"
        "padding:30px;"
        "border-radius:18px;"
        "box-shadow:0 10px 30px rgba(0,0,0,.07);"
        "}"

        ".grid{"
        "display:grid;"
        "grid-template-columns:repeat(2,1fr);"
        "gap:15px;"
        "margin-top:25px;"
        "}"

        ".item{"
        "background:#f8fafc;"
        "padding:18px;"
        "border-radius:10px;"
        "}"

        ".label{"
        "font-size:13px;"
        "color:#64748b;"
        "}"

        ".value{"
        "font-size:17px;"
        "font-weight:bold;"
        "margin-top:5px;"
        "}"

        ".wide{"
        "grid-column:1/-1;"
        "}"

        ".history{"
        "white-space:pre-wrap;"
        "font-weight:normal;"
        "line-height:1.7;"
        "}"

        ".button{"
        "display:inline-block;"
        "margin-top:20px;"
        "margin-right:10px;"
        "padding:12px 20px;"
        "background:#2563eb;"
        "color:white;"
        "text-decoration:none;"
        "border-radius:8px;"
        "}"

        "@media(max-width:700px){"
        ".grid{grid-template-columns:1fr;}"
        "}"

        "</style>"
        "</head>"

        "<body>"

        "<div class='box'>"

        "<h1>Complaint Details</h1>"

        "<div class='grid'>"

        "<div class='item'>"
        "<div class='label'>Complaint ID</div>"
        "<div class='value'>C%d</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Complaint Type</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item wide'>"
        "<div class='label'>Complaint</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Affected People</div>"
        "<div class='value'>%d</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Priority</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Resolution Time</div>"
        "<div class='value'>%d hours</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Assigned To</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Status</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item'>"
        "<div class='label'>Escalation</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item wide'>"
        "<div class='label'>Resolution</div>"
        "<div class='value'>%s</div>"
        "</div>"

        "<div class='item wide'>"
        "<div class='label'>History</div>"
        "<div class='value history'>%s</div>"
        "</div>"

        "</div>"

        "<a class='button' href='/track'>Track Another</a>"
        "<a class='button' href='/'>Student Portal</a>"

        "</div>"

        "</body>"
        "</html>",

        c->complaintId,
        safeType,
        safeComplaint,
        c->affected,
        priorityText(c->priority),
        c->resolutionHours,
        safeAssigned,
        safeStatus,
        safeEscalation,
        safeResolution,
        safeHistory
    );

    sendResponse(client, html);
}


/* =========================================================
   RECEIVE HTTP REQUEST
   ========================================================= */

int receiveRequest(
    SOCKET client,
    char *request,
    int size
)
{
    int total = 0;

    while (total < size - 1)
    {
        int received =
            recv(
                client,
                request + total,
                size - 1 - total,
                0
            );

        if (received <= 0)
            return 0;

        total += received;

        request[total] = '\0';

        if (
            strstr(
                request,
                "\r\n\r\n"
            ) != NULL
        )
        {
            break;
        }
    }

    return total;
}


/* =========================================================
   CLIENT HANDLER
   ========================================================= */

void handleClient(
    SOCKET client
)
{
    char request[REQUEST_SIZE];

    memset(
        request,
        0,
        sizeof(request)
    );

    if (
        receiveRequest(
            client,
            request,
            sizeof(request)
        ) <= 0
    )
    {
        return;
    }


    /* METHOD */

    char method[20];
    char path[200];

    sscanf(
        request,
        "%19s %199s",
        method,
        path
    );


    /* BODY */

    char *body =
        strstr(
            request,
            "\r\n\r\n"
        );

    if (body != NULL)
        body += 4;
    else
        body = "";


    /* SESSION */

    int sessionIndex =
        findSession(request);


    /* =====================================================
       GET
       ===================================================== */

    if (strcmp(method, "GET") == 0)
    {
        /* LOGIN */

        if (strcmp(path, "/login") == 0)
        {
            if (sessionIndex >= 0)
            {
                if (
                    sessions[sessionIndex].role ==
                    ADMIN_ROLE
                )
                {
                    sendRedirect(
                        client,
                        "/admin"
                    );
                }
                else
                {
                    sendRedirect(
                        client,
                        "/"
                    );
                }

                return;
            }

            serveFile(
                client,
                "login.html"
            );

            return;
        }


        /* REGISTER */

        if (strcmp(path, "/register") == 0)
        {
            if (sessionIndex >= 0)
            {
                sendRedirect(client, "/");
                return;
            }

            serveRegisterPage(client);
            return;
        }


        /* LOGOUT */

        if (strcmp(path, "/logout") == 0)
        {
            if (sessionIndex >= 0)
            {
                sessions[sessionIndex].active = 0;
            }

            sendLogoutResponse(client);

            return;
        }


        /* STUDENT HOME */

        if (strcmp(path, "/") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    STUDENT_ROLE
                )
            )
                return;

            serveIndex(
                client,
                &sessions[sessionIndex]
            );

            return;
        }


        /* SUBMIT */

        if (strcmp(path, "/submit") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    STUDENT_ROLE
                )
            )
                return;

            serveFile(
                client,
                "submit.html"
            );

            return;
        }


        /* TRACK */

        if (strcmp(path, "/track") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    STUDENT_ROLE
                )
            )
                return;

            serveFile(
                client,
                "track.html"
            );

            return;
        }


        /* ADMIN DASHBOARD */

        if (strcmp(path, "/admin") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    ADMIN_ROLE
                )
            )
                return;

            adminDashboard(client);

            return;
        }


        /* PROCESS */

        if (strcmp(path, "/process") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    ADMIN_ROLE
                )
            )
                return;

            processNextComplaint(client);

            return;
        }


        /* ESCALATION */

        if (strcmp(path, "/escalation") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    ADMIN_ROLE
                )
            )
                return;

            handleEscalationRoute(client);

            return;
        }


        sendError(
            client,
            "Page not found."
        );

        return;
    }


    /* =====================================================
       POST
       ===================================================== */

    if (strcmp(method, "POST") == 0)
    {
        /* LOGIN */

        if (strcmp(path, "/login") == 0)
        {
            handleLogin(
                client,
                body
            );

            return;
        }


        /* REGISTER */

        if (strcmp(path, "/register") == 0)
        {
            if (sessionIndex >= 0)
            {
                sendRedirect(client, "/");
                return;
            }

            handleRegister(client, body);
            return;
        }


        /* SUBMIT COMPLAINT */

        if (strcmp(path, "/submit") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    STUDENT_ROLE
                )
            )
                return;

            handleSubmit(
                client,
                body,
                &sessions[sessionIndex]
            );

            return;
        }


        /* TRACK */

        if (strcmp(path, "/track") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    STUDENT_ROLE
                )
            )
                return;

            showTrackedComplaint(
                client,
                body,
                &sessions[sessionIndex]
            );

            return;
        }


        /* UPDATE */

        if (strcmp(path, "/update") == 0)
        {
            if (
                !requireRole(
                    client,
                    sessionIndex,
                    ADMIN_ROLE
                )
            )
                return;

            updateComplaint(
                client,
                body
            );

            return;
        }


        sendError(
            client,
            "Invalid POST request."
        );

        return;
    }


    sendError(
        client,
        "Unsupported HTTP method."
    );
}


/* =========================================================
   FREE MEMORY
   ========================================================= */

void freeMemory(void)
{
    struct Complaint *current = head;

    while (current != NULL)
    {
        struct Complaint *next =
            current->next;

        free(current);

        current = next;
    }

    head = NULL;
    front = NULL;
}


/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    WSADATA wsa;

    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsa
        ) != 0
    )
    {
        printf("WSAStartup failed.\n");
        return 1;
    }


    srand(
        (unsigned int)(
            time(NULL) ^
            GetTickCount()
        )
    );


    loadStudentAccounts();
    loadComplaints();


    SOCKET serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (serverSocket == INVALID_SOCKET)
    {
        printf("Socket creation failed.\n");

        WSACleanup();

        return 1;
    }


    struct sockaddr_in serverAddress;

    memset(
        &serverAddress,
        0,
        sizeof(serverAddress)
    );

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_addr.s_addr =
        INADDR_ANY;

    serverAddress.sin_port =
        htons(PORT);


    if (
        bind(
            serverSocket,
            (struct sockaddr *)&serverAddress,
            sizeof(serverAddress)
        ) == SOCKET_ERROR
    )
    {
        printf("Bind failed.\n");

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    if (
        listen(
            serverSocket,
            10
        ) == SOCKET_ERROR
    )
    {
        printf("Listen failed.\n");

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    printf("\n");
    printf("========================================\n");
    printf(" Student Complaint Prioritization System\n");
    printf("========================================\n");
    printf("Server running on port %d\n", PORT);
    printf("Open: http://localhost:%d/login\n", PORT);
    printf("\n");
    printf("Student Login:\n");
    printf("Registered students can use their own account.\n");
    printf("New students: http://localhost:%d/register\n", PORT);
    printf("\n");
    printf("Admin Login:\n");
    printf("admin / admin123\n");
    printf("========================================\n\n");


    while (1)
    {
        struct sockaddr_in clientAddress;

        int clientLength =
            sizeof(clientAddress);

        SOCKET client =
            accept(
                serverSocket,
                (struct sockaddr *)&clientAddress,
                &clientLength
            );

        if (client == INVALID_SOCKET)
        {
            continue;
        }


        handleClient(client);


        shutdown(
            client,
            SD_BOTH
        );

        closesocket(client);
    }


    freeMemory();

    closesocket(serverSocket);

    WSACleanup();

    return 0;
}