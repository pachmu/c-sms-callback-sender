#include "main.h"

int main(int argc, char *argv[]){

	int rez = 0;
	char path[MAX_INI_PATH_LENGTH]= "config.ini";

	while ( (rez = getopt(argc,argv,"c:d")) != ERROR_VAL){
		switch (rez){
			case 'c':
				strcpy(path, optarg);
				break;
			case 'd':
				debug = 1;
				break;
			case '?':
				return ERROR_VAL;
				break;
		}
	}


	pid_t sid;

	if(debug == 0){

		int pid = fork();

		if (pid < 0) {
			logMsg(LOG_ERR, "Unable to start process");
			return ERROR_VAL;
		}

		if (pid > 0) {
			logMsg(LOG_ERR, "Process start");
			return ERROR_VAL;
		}

		sid = setsid();

		if (sid < 0) {
			logMsg(LOG_ERR, "Unable to set sid");
			return ERROR_VAL;
		}

//		if ((chdir("/")) < 0) {
//			logMsg(LOG_ERR, "Unable to change dir");
//			return ERROR_VAL;
//		}
		fclose(stdout);
		fclose(stderr);

	}
	if (signal(SIGINT, signalHendler) == SIG_ERR) {
		logMsg(LOG_ERR, "An error occurred while setting a signal handler");
		return ERROR_VAL;
	}

	memset(&conf, 0, sizeof(conf));

	if(readConfig(path)<0)
		return ERROR_VAL;


	pidFilehandle = open(conf.pidfilePath, O_RDWR | O_CREAT, 0600);

	if (pidFilehandle == -1 ){

		logMsg(LOG_INFO, "Process is already running");
		return ERROR_VAL;
	}

	if (lockf(pidFilehandle,F_TLOCK,0) == -1){

		logMsg(LOG_INFO, "Process is already running");
		return ERROR_VAL;

	}
    char str[10];
	sprintf(str,"%d\n",getpid());

	write(pidFilehandle, str, strlen(str));

	if(process() < 0)
		return ERROR_VAL;

	free(partnerCache);
	close(pidFilehandle);

	return OK_VAL;
}

/**
 *  Function starts main process
 */

int process()
{
	MYSQL mysql;
	MYSQL_STMT *stmtGetRows;
	MYSQL_STMT *stmtUpdateRows;
	MYSQL_STMT *stmtSelectUrls;

	workerId = 1;


	while(inProcess == 1){

		sem_init(&semaphore, 0, atoi(conf.maxThreads));

		if( mysqlConnect(&mysql) > ERROR_VAL){

			stmtGetRows = mysql_stmt_init(&mysql);
			stmtUpdateRows = mysql_stmt_init(&mysql);
			stmtSelectUrls = mysql_stmt_init(&mysql);

			if( mysqlSTMTConnect(stmtGetRows, stmtUpdateRows, stmtSelectUrls) > ERROR_VAL ){

				while(inProcess == 1){

					if(partnerUrlUpdate(stmtSelectUrls) < 0 ||
							mysqlGetRows(stmtGetRows, stmtUpdateRows) < 0 ){

						break;
					}
				}
				int threadCount = 0;
				while(1){
					sem_getvalue(&semaphore, &threadCount);
					if(threadCount == atoi(conf.maxThreads)){
						break;
					}
					usleep(100000);
				}
			}
			mysqlSTMTCloseConnection(stmtGetRows);
			mysqlSTMTCloseConnection(stmtUpdateRows);
			mysqlSTMTCloseConnection(stmtSelectUrls);

		}
		mysqlCloseConnection(&mysql);
		logMsg(LOG_DEBUG, "Mysql connection close");

		mysql_library_end();
		sem_destroy(&semaphore);
		if(inProcess){
			usleep(atoi(conf.errorWaitTime));
		}
	}

	return OK_VAL;

}

/**
 *  Function gets rows from mysql
 */

int mysqlGetRows(MYSQL_STMT *stmtGetRows, MYSQL_STMT *stmtUpdateRows)
{


	workerId ++;
	if(workerId > MAX_WORKER_ID){
		workerId = 1;
	}
	int status;

	if(mysql_stmt_execute(stmtUpdateRows)){
		logMsg(LOG_ERR, mysql_stmt_error(stmtUpdateRows));
		return ERROR_VAL;
	}
	if (!(mysql_stmt_fetch(stmtUpdateRows))){
		logMsg(LOG_ERR, mysql_stmt_error(stmtUpdateRows));
		return ERROR_VAL;
	}

	//Get mysql procedure result

	if(mysql_stmt_execute(stmtGetRows)){
		logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
		return ERROR_VAL;
	}
	if (mysql_stmt_store_result(stmtGetRows)){
		logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
		return ERROR_VAL;
	}

	MYSQL_RES* prepare_meta_result = mysql_stmt_result_metadata(stmtGetRows);
	if (!prepare_meta_result){
		logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
		return ERROR_VAL;
	}

	//Decl number of rows and fields in result table
	rowNumber = mysql_stmt_num_rows(stmtGetRows);
	const int fieldsNumber = mysql_num_fields(prepare_meta_result);

    char callbackRows[MAX_FIELDS_VALUES_LENGTH * fieldsNumber * rowNumber];
    memset(callbackRows, 0 , sizeof(callbackRows));

    if(rowNumber > 0){
    	int resCount;

    	for(resCount = 0; resCount < rowNumber; resCount++){

    		MYSQL_BIND result[fieldsNumber];
			my_bool isNull[fieldsNumber], error[fieldsNumber];
			unsigned long len[fieldsNumber];

			int fieldsCount;

			for(fieldsCount = 0; fieldsCount < fieldsNumber; fieldsCount++){

				int iterator = fieldsCount + fieldsNumber * resCount;

				result[fieldsCount].buffer_type = MYSQL_TYPE_VAR_STRING;
				result[fieldsCount].buffer =  (char *) &callbackRows[iterator * MAX_FIELDS_VALUES_LENGTH];
				result[fieldsCount].buffer_length = MAX_FIELDS_VALUES_LENGTH;
				result[fieldsCount].is_null= &isNull[fieldsCount];
				result[fieldsCount].length= &len[fieldsCount];
				result[fieldsCount].error = &error[fieldsCount];

			}
			if(mysql_stmt_bind_result(stmtGetRows, result)){
				logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
				return ERROR_VAL;
			}


			status = mysql_stmt_fetch(stmtGetRows);

			if (status == 1 || status == MYSQL_NO_DATA)
				break;


    	}
    	if(threadCreator(callbackRows, fieldsNumber, rowNumber) == ERROR_VAL ){
			return ERROR_VAL;
		}

    }
    if (rowNumber == 0 || rowNumber < atoi(conf.rowsCount)){
		usleep(atoi(conf.waitTime));
	}

    mysql_free_result(prepare_meta_result);
    mysql_stmt_next_result(stmtGetRows);

	return OK_VAL;

}

int threadCreator(char *callbackRows, const int fieldsNumber, const int rowNumber)
{

	thread_data *data = malloc(sizeof(thread_data));
	int rowsBlockSize = MAX_FIELDS_VALUES_LENGTH * fieldsNumber * rowNumber;
	char *threadRows = malloc(rowsBlockSize);
	memcpy(threadRows, callbackRows, (size_t)rowsBlockSize );


	pthread_t thread;

	sem_wait(&semaphore);

	data->fieldsNumber = fieldsNumber;
	data->rowNumber = rowNumber;
	data->callbackRows = threadRows;

//			if(sendCallbacks((void *)&data) == ERROR_VAL)
//				return ERROR_VAL;


	int rc = pthread_create(&thread, NULL, sendCallbacks, (void *)data);
	pthread_detach(thread);
	if (rc){
		logMsg(LOG_ERR, "ERROR return code from pthread_create()");

		sem_post(&semaphore);
	    return ERROR_VAL;
	}


	return OK_VAL;

}
/**
 * Function runs in thread
 */
void *sendCallbacks(void *threadarg)
{
	thread_data *data = (thread_data *) threadarg;
	while(1){

		connections threadConnection;
		memset(&threadConnection, 0, sizeof(threadConnection));

		if(makeThreadConnection(&threadConnection) == ERROR_VAL){
			closeThreadConnection(&threadConnection);
			break;
		}
		curl_global_init(CURL_GLOBAL_ALL);

		int rows;
		char sentStatus[STATUS_LENGTH];
		memset(&sentStatus, 0, sizeof(sentStatus));

		for(rows = 0; rows < data->rowNumber; rows++) {

			sentStatus[0] = 'N';

			char url[MAX_QUERY_LENGTH];
			memset(&url, 0, sizeof(url));
			char *msisdn;

			char *currentRow = &data->callbackRows[data->fieldsNumber * rows * MAX_FIELDS_VALUES_LENGTH];
			char *smsId = currentRow + FIELD_NUM_SMS_ID * MAX_FIELDS_VALUES_LENGTH;
			strcpy(threadConnection.smsId, smsId);
			threadConnection.partnerId = atoi(currentRow + FIELD_NUM_PARTNER_ID * MAX_FIELDS_VALUES_LENGTH);
			threadConnection.rowId = atoi(currentRow + FIELD_NUM_ROW_ID * MAX_FIELDS_VALUES_LENGTH);
			threadConnection.attempts = atoi(currentRow + FIELD_NUM_ATTEMPTS * MAX_FIELDS_VALUES_LENGTH);
			int deliveryStatus = atoi(currentRow + FIELD_NUM_ROW_STATUS * MAX_FIELDS_VALUES_LENGTH);
			msisdn = currentRow + FIELD_NUM_ROW_MSISDN * MAX_FIELDS_VALUES_LENGTH;


			int partnersRows;

			for(partnersRows = 0; partnersRows < partnersCount; partnersRows++){
				if(partnerCache[partnersRows].partnerId == threadConnection.partnerId) {
					sprintf(url, "%s", partnerCache[partnersRows].partnerUrl);
					break;
				}

			}
			if(strlen(url) == 0 || strlen(msisdn) == 0) {
				sentStatus[0] = 'E';
				threadConnection.attempts = atoi(conf.attempts) - 1;
			}



			if(sentStatus[0] == 'N') {
				if(httpRequest(url, msisdn, threadConnection.smsId, deliveryStatus) > ERROR_VAL){
					sentStatus[0] = 'Y';
				} else {
					sentStatus[0] = 'E';
				}
			} else {
				sentStatus[0] = 'E';
			}
			{
				threadConnection.attempts++;
				strcpy(threadConnection.sentStatus, sentStatus);

				int tryCounter;
				for(tryCounter = 0; tryCounter < DEAD_LOCK_TRY_COUNT; tryCounter++){

					if(mysql_stmt_execute(threadConnection.stmtUpdateRow)){
						logMsg(LOG_ERR, mysql_stmt_error(threadConnection.stmtUpdateRow));
						usleep(100);
						continue;
					}
					if (!(mysql_stmt_fetch(threadConnection.stmtUpdateRow))){
						logMsg(LOG_ERR, mysql_stmt_error(threadConnection.stmtUpdateRow));
						continue;
					}

					break;
				}
			}

		}

		curl_global_cleanup();
		mysql_thread_end();

		closeThreadConnection(&threadConnection);



		break;
	}

	free(data->callbackRows);
	free(data);

	sem_post(&semaphore);

	return OK_VAL;

}

int partnerUrlUpdate(MYSQL_STMT *stmtSelectUrls)
{
	int updateUrlTimeoutSec = atoi(conf.partnerUrlUpdateTimeout) * 60;
	if(urlUpdateTime +  updateUrlTimeoutSec < (int)time(NULL)){
		int threadCount = 0;
		while(1){
			sem_getvalue(&semaphore, &threadCount);
			if(threadCount == atoi(conf.maxThreads)){
				break;
			}
			usleep(100000);
		}
		urlUpdateTime = (int)time(NULL);

		if(mysql_stmt_execute(stmtSelectUrls)){
			logMsg(LOG_ERR, mysql_stmt_error(stmtSelectUrls));
			return ERROR_VAL;;
		}
		if (mysql_stmt_store_result(stmtSelectUrls)){
			logMsg(LOG_ERR, mysql_stmt_error(stmtSelectUrls));
			return ERROR_VAL;;
		}

		MYSQL_RES* prepare_meta_result_url = mysql_stmt_result_metadata(stmtSelectUrls);
		if (!prepare_meta_result_url){
			logMsg(LOG_ERR, mysql_stmt_error(stmtSelectUrls));
			return ERROR_VAL;
		}

		//Decl number of rows and fields in result table
		partnersCount = mysql_stmt_num_rows(stmtSelectUrls);

		char url[MAX_FIELDS_URL_VALUE_LENGTH];
		int id;

		MYSQL_BIND resultUrl[2];
		my_bool isNullUrl[2], errorUrl[2];
		unsigned long lenUrl[2];

		resultUrl[0].buffer_type = MYSQL_TYPE_VAR_STRING;
		resultUrl[0].buffer = (char *) &url[0];
		resultUrl[0].buffer_length = MAX_FIELDS_URL_VALUE_LENGTH;
		resultUrl[0].is_null= isNullUrl;
		resultUrl[0].length= lenUrl;
		resultUrl[0].error = errorUrl;

		resultUrl[1].buffer_type = MYSQL_TYPE_LONG;
		resultUrl[1].buffer =  &id;
		resultUrl[1].is_null= isNullUrl;
		resultUrl[1].length= lenUrl;
		resultUrl[1].error = errorUrl;


		if(mysql_stmt_bind_result(stmtSelectUrls, resultUrl)){
			logMsg(LOG_ERR, mysql_stmt_error(stmtSelectUrls));
			return ERROR_VAL;;
		}

		free(partnerCache);
		partnerCache = malloc(partnersCount * sizeof(partner));
		memset(partnerCache, 0, partnersCount * sizeof(partner));

		int urlCounter;
		for(urlCounter = 0; urlCounter < partnersCount; urlCounter++) {
			memset(url, 0, MAX_FIELDS_URL_VALUE_LENGTH);
			int statusUrl = mysql_stmt_fetch(stmtSelectUrls);

			if (statusUrl == 1 || statusUrl == MYSQL_NO_DATA)
				break;

			partnerCache[urlCounter].partnerId = id;
			sprintf(partnerCache[urlCounter].partnerUrl, "%s", url);

		}
		mysql_free_result(prepare_meta_result_url);
		mysql_stmt_next_result(stmtSelectUrls);

	}
	return OK_VAL;
}

int httpRequest(char *url, char *msisdn, char *smsId, int status)
{
	CURL *curl;
	CURLcode result;
	char errorBuffer[CURL_ERROR_SIZE];

	char message[MAX_QUERY_LENGTH];
	sprintf(message, "sms_id=%s&idofsms=%s&resource_id=%s&msisdn=%s&subscriber=%s&delivery_status=%d&status=%d",
						smsId, smsId, smsId, msisdn,msisdn,status,status);

	curl = curl_easy_init();

	struct curl_slist *headers = NULL;

	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	headers = curl_slist_append(headers, "charsets:utf-8");
	//headers = curl_slist_append(headers, "Connection: keep-alive");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	//curl_easy_setopt(curl, CURLOPT_VERBOSE, TRUE);

	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, atoi(conf.socketWaitTimeout));

	curl_easy_setopt(curl, CURLOPT_TIMEOUT, atoi(conf.socketTimeout));

    curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ignoreOutputHendler);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

	result = curl_easy_perform(curl);

	if(result != CURLE_OK){
		logMsg(LOG_ERR, "Curl request failed");
		logMsg(LOG_ERR, curl_easy_strerror(result));
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return ERROR_VAL;
	}

	long httpCode = 0;
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &httpCode);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if(httpCode != 200){
		return ERROR_VAL;
	}

	return OK_VAL;
}


int mysqlConnect(MYSQL *mysql)
{
	if(mysql_library_init(0, NULL, NULL)){
		logMsg(LOG_ERR, "MySQL library init error\n");
		return ERROR_VAL;
	}
	mysql_init(mysql);

	if(!mysql){
		logMsg(LOG_ERR, "MySQL init error\n");
		return ERROR_VAL;
	}

	unsigned int wait_timeout = 10;
	mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &wait_timeout);

	if (!(mysql_real_connect(mysql, conf.mysqlHost,conf.mysqlUser, conf.mysqlPassword, conf.mysqlDb,
								atoi(conf.mysqlPort), NULL, CLIENT_MULTI_RESULTS))){
		logMsg(LOG_ERR, mysql_error(mysql));
		return ERROR_VAL;
	}
	if (mysql_set_character_set(mysql, "utf8")) {
		logMsg(LOG_ERR, "Charset was not set");
		return ERROR_VAL;
	}
	return OK_VAL;

}

int mysqlSTMTConnect(MYSQL_STMT *stmtGetRows, MYSQL_STMT *stmtUpdateRows, MYSQL_STMT *stmtSelectUrls)
{

	if (!stmtGetRows || !stmtUpdateRows ||  !stmtSelectUrls)
	{
		logMsg(LOG_ERR, "Could not initialize statement\n");
		return ERROR_VAL;
	}

	char query[MAX_QUERY_LENGTH];
	{
		MYSQL_BIND bindGetRows[1];
		sprintf(query, "SELECT sms_id, status, updated, partner_id, id, attempts, msisdn FROM sms_outbox_cb WHERE worker = ? AND sent = 'O' ORDER BY updated DESC LIMIT %d;", atoi(conf.rowsCount));

		if( mysql_stmt_prepare(stmtGetRows, query, MAX_QUERY_LENGTH) ){

			logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
			return ERROR_VAL;
		}
		memset(bindGetRows, 0, sizeof (bindGetRows));

		bindGetRows[0].buffer_type = MYSQL_TYPE_LONG;
		bindGetRows[0].buffer = (char *)&workerId;
		bindGetRows[0].length = 0;
		bindGetRows[0].is_null = 0;


		if ( mysql_stmt_bind_param(stmtGetRows, bindGetRows) ){

			logMsg(LOG_ERR, mysql_stmt_error(stmtGetRows));
			return ERROR_VAL;
		}
	}
	{
		MYSQL_BIND bindUpdateWorker[1];
		sprintf(query, "UPDATE sms_outbox_cb SET worker = ?, updated = CURRENT_TIMESTAMP(), sent = 'O' "
				"WHERE (sent = 'O' AND updated < CURRENT_TIMESTAMP() - INTERVAL %d SECOND) "
				"OR (sent = 'E' AND attempts < %d) "
				"OR (sent = 'N' AND worker = 0) LIMIT %d;",
				atoi(conf.attemptsWaitTime),atoi(conf.attempts), atoi(conf.rowsCount));

		if( mysql_stmt_prepare(stmtUpdateRows, query, MAX_QUERY_LENGTH) ){

			logMsg(LOG_ERR, mysql_stmt_error(stmtUpdateRows));
			return ERROR_VAL;
		}
		memset(bindUpdateWorker, 0, sizeof (bindUpdateWorker));

		bindUpdateWorker[0].buffer_type = MYSQL_TYPE_LONG;
		bindUpdateWorker[0].buffer = (char *)&workerId;
		bindUpdateWorker[0].length = 0;
		bindUpdateWorker[0].is_null = 0;


		if ( mysql_stmt_bind_param(stmtUpdateRows, bindUpdateWorker) ){

			logMsg(LOG_ERR, mysql_stmt_error(stmtUpdateRows));
			return ERROR_VAL;
		}
	}
	{
		char selectUrlQuery[MAX_QUERY_LENGTH] = "SELECT callback_url, id FROM partner;";

		if( mysql_stmt_prepare(stmtSelectUrls, selectUrlQuery, MAX_QUERY_LENGTH ) ){

			logMsg(LOG_ERR, mysql_stmt_error(stmtSelectUrls));
			return ERROR_VAL;
		}
	}

	logMsg(LOG_DEBUG, "Mysql init ok");

	return OK_VAL;
}

int makeThreadConnection(connections *threadConn)
{

	if( mysqlConnect(&threadConn->conn) == ERROR_VAL){
		logMsg(LOG_ERR, "Thread connection init error");
		return ERROR_VAL;
	}


	{
		MYSQL_BIND bindUpdateRow[3];
		threadConn->stmtUpdateRow = mysql_stmt_init(&threadConn->conn);

		char updateRowQuery[MAX_QUERY_LENGTH] = "UPDATE sms_outbox_cb SET sent = ?, attempts = ? WHERE id = ?;";

		if( mysql_stmt_prepare(threadConn->stmtUpdateRow, updateRowQuery, MAX_QUERY_LENGTH ) ){

			logMsg(LOG_ERR, mysql_stmt_error(threadConn->stmtUpdateRow));
			return ERROR_VAL;
		}

		threadConn->sentStatus[0] = 'N';

		memset(bindUpdateRow, 0, sizeof (bindUpdateRow));

		bindUpdateRow[0].buffer_type = MYSQL_TYPE_STRING;
		bindUpdateRow[0].buffer = &threadConn->sentStatus;
		bindUpdateRow[0].buffer_length = 1;
		bindUpdateRow[0].length = 0;
		bindUpdateRow[0].is_null = 0;

		bindUpdateRow[1].buffer_type = MYSQL_TYPE_LONG;
		bindUpdateRow[1].buffer = &threadConn->attempts;
		bindUpdateRow[1].length = 0;
		bindUpdateRow[1].is_null = 0;

		bindUpdateRow[2].buffer_type = MYSQL_TYPE_LONG;
		bindUpdateRow[2].buffer = &threadConn->rowId;
		bindUpdateRow[2].length = 0;
		bindUpdateRow[2].is_null = 0;

		if ( mysql_stmt_bind_param(threadConn->stmtUpdateRow, bindUpdateRow) ){

			logMsg(LOG_ERR, mysql_stmt_error(threadConn->stmtUpdateRow));
			return ERROR_VAL;
		}
	}

	return OK_VAL;

}

int closeThreadConnection(connections *threadConn)
{
	mysqlSTMTCloseConnection(threadConn->stmtUpdateRow);

	mysqlCloseConnection(&threadConn->conn);

	return OK_VAL;

}

int mysqlCloseConnection(MYSQL *mysql)
{
	if(mysql)
		mysql_close(mysql);


//	logMsg(LOG_DEBUG, "Mysql close connection");

	return OK_VAL;
}

int mysqlSTMTCloseConnection (MYSQL_STMT *stmt) {

	if(stmt)
		mysql_stmt_close(stmt);

//	logMsg(LOG_DEBUG, "Mysql STMT close connection");

	return OK_VAL;

}


/**
 *  Function reads config from file and writes to config structure
 */

int readConfig(char *path)
{

	if (ini_parse(path, configIniHandler, &conf) < 0 ) {
		logMsg(LOG_ERR, "Can't load config\n");
		return ERROR_VAL;
	}

	return OK_VAL;

}

size_t ignoreOutputHendler( void *ptr, size_t size, size_t nmemb, void *stream)
{
    (void) ptr;
    (void) stream;
    return size * nmemb;
}


static void signalHendler(int signo)
{

	switch(signo){

	case SIGTERM:
	case SIGABRT:
	case SIGINT:

		inProcess = 0;
		break;

	}
}

static int configIniHandler(void *user, const char *section, const char *name, const char *value)
{

	config *conf = (config*) user;

		//Настройки MySQL
    if (MATCH("mysql_config", "mysql_host")) {
    	strcpy(conf->mysqlHost, value);

    } else if (MATCH("mysql_config", "mysql_port")) {
    	strcpy(conf->mysqlPort, value);

    } else if (MATCH("mysql_config", "mysql_user")) {
    	strcpy(conf->mysqlUser, value);

    } else if (MATCH("mysql_config", "mysql_password")) {
    	strcpy(conf->mysqlPassword, value);

    } else if (MATCH("mysql_config", "mysql_db")) {
    	strcpy(conf->mysqlDb, value);

    } else if (MATCH("mysql_config", "mysql_table")) {
        strcpy(conf->mysqlTable, value);


		//Настройки демона
	} else if (MATCH("daemon_config", "rows_count")) {
		strcpy(conf->rowsCount, value);

	} else if (MATCH("daemon_config", "wait_time")) {
		strcpy(conf->waitTime, value);

	} else if (MATCH("daemon_config", "error_wait_time")) {
		strcpy(conf->errorWaitTime, value);

	} else if (MATCH("daemon_config", "max_threads")) {
		strcpy(conf->maxThreads, value);

	} else if (MATCH("daemon_config", "socket_timeout")) {
		strcpy(conf->socketTimeout, value);

	} else if (MATCH("daemon_config", "socket_wait_timeout")) {
		strcpy(conf->socketWaitTimeout, value);

	} else if (MATCH("daemon_config", "journal_name")) {
		strcpy(conf->journalName, value);

	} else if (MATCH("daemon_config", "attempts")) {
		strcpy(conf->attempts, value);

	} else if (MATCH("daemon_config", "attempts_wait_time")) {
		strcpy(conf->attemptsWaitTime, value);

	} else if (MATCH("daemon_config", "pidfile_path")) {
		strcpy(conf->pidfilePath, value);

	} else if (MATCH("daemon_config", "partner_url_update_timeout")) {
		strcpy(conf->partnerUrlUpdateTimeout, value);


    } else {
        //return OK_VAL;  // unknown name
	}
    return OK_VAL;
}


int logMsg(int priority, const char *logMessage)
{
	if(!debug){
		if(priority != LOG_DEBUG){
			openlog(conf.journalName, 0, LOG_USER);
			syslog(priority, logMessage);
			closelog();
		}

	}else{

		printf(logMessage);
		printf("\n");

	}

	return OK_VAL;
}

