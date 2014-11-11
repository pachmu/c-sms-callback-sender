#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>


#include <mysql/mysql.h>
#include "ini.h"
#include <pthread.h>
#include <semaphore.h>
#include <curl/curl.h>


#define ERROR_VAL -1
#define OK_VAL 0

#define DEFAULT_PROGRAMM_NAME "callback_sender"
#define MAX_CONFIG_PARAM_LENGTH 100
#define MAX_QUERY_LENGTH 10000
#define MAX_INI_PATH_LENGTH 100

#define FIELDS_COUNT 24
#define SMS_ID_LENGTH 37
#define MAX_WORKER_ID 127

#define FIELD_NUM_PARTNER_ID 3
#define FIELD_NUM_SMS_ID 0
#define FIELD_NUM_ROW_ID 4
#define FIELD_NUM_ROW_STATUS 1
#define FIELD_NUM_ATTEMPTS 5
#define FIELD_NUM_ROW_MSISDN 6

#define STATUS_LENGTH 2

#define MAX_FIELDS_VALUES_LENGTH 50
#define MAX_FIELDS_URL_VALUE_LENGTH 1000
#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

#define MAX_BUF	65536
#define DEAD_LOCK_TRY_COUNT 5


typedef struct {

	char mysqlHost[MAX_CONFIG_PARAM_LENGTH];
	char mysqlPort[MAX_CONFIG_PARAM_LENGTH];
	char mysqlUser[MAX_CONFIG_PARAM_LENGTH];
	char mysqlPassword[MAX_CONFIG_PARAM_LENGTH];
	char mysqlDb[MAX_CONFIG_PARAM_LENGTH];
	char mysqlTable[MAX_CONFIG_PARAM_LENGTH];


	char rowsCount[MAX_CONFIG_PARAM_LENGTH];
	char daemonName[MAX_CONFIG_PARAM_LENGTH];
	char waitTime[MAX_CONFIG_PARAM_LENGTH];
	char errorWaitTime[MAX_CONFIG_PARAM_LENGTH];
	char maxThreads[MAX_CONFIG_PARAM_LENGTH];
	char socketTimeout[MAX_CONFIG_PARAM_LENGTH];
	char socketWaitTimeout[MAX_CONFIG_PARAM_LENGTH];
	char journalName[MAX_CONFIG_PARAM_LENGTH];
	char attempts[MAX_CONFIG_PARAM_LENGTH];
	char attemptsWaitTime[MAX_CONFIG_PARAM_LENGTH];
	char pidfilePath[MAX_CONFIG_PARAM_LENGTH];
	char partnerUrlUpdateTimeout[MAX_CONFIG_PARAM_LENGTH];


} config;

typedef struct {
	MYSQL conn;

	MYSQL_STMT *stmtUpdateRow;

	int rowId;
	char smsId[SMS_ID_LENGTH];
	int partnerId;
	char sentStatus[STATUS_LENGTH];
	int attempts;

} connections;

typedef struct {
   int fieldsNumber;
   int rowNumber;
   char *callbackRows;

} thread_data;

typedef struct {
   int partnerId;
   char partnerUrl[MAX_FIELDS_URL_VALUE_LENGTH];

} partner;

int pidFilehandle;

config conf;
int workerId;

int rowNumber;

int debug=0;
int inProcess = 1;

int urlUpdateTime = 0;

sem_t semaphore;

partner *partnerCache;
int partnersCount;

int process();
int logMsg(int priority, const char *logMessage);
int readConfig(char *path);
static int configIniHandler(void *user, const char *section, const char *name, const char *value);
static void signalHendler(int signo);
size_t ignoreOutputHendler( void *ptr, size_t size, size_t nmemb, void *stream);

int mysqlConnect(MYSQL *mysql);
int mysqlSTMTConnect(MYSQL_STMT *stmtGetRows, MYSQL_STMT *stmtUpdateRows, MYSQL_STMT *stmtSelectUrls);
int mysqlSTMTCloseConnection(MYSQL_STMT *stmtGetRows);
int mysqlCloseConnection(MYSQL *mysql);
int mysqlGetRows(MYSQL_STMT *stmtGetRows, MYSQL_STMT *stmtUpdateRows);

int makeThreadConnection();
int closeThreadConnection(connections *threadConn);
int threadCreator(char *callbackRows, int fieldsNumber, int rowNumber);
void *sendCallbacks(void *threadarg);

int httpRequest(char *url, char *msisdn, char *smsId, int status);
int partnerUrlUpdate(MYSQL_STMT *stmtSelectUrls);


