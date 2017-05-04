// LSM.Test.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include "lsm.h"

#include <Windows.h>
#include <string>       // std::string
#include <iostream>     // std::cout
#include <sstream>      // std::stringstream
#include <iomanip>
#include <thread>

using namespace std;

double runInserts(lsm_db *db, int start, int end) {
  long long keyLength = 0;
  long long valueLength = 0;
  LARGE_INTEGER sum = { 0 };
  LARGE_INTEGER pcFreq = { 0 };
  
  QueryPerformanceFrequency(&pcFreq);
  double freq = double(pcFreq.QuadPart) / 1000;
  ULONG failedAttempts = 0;

  for (int i = start; i < end; i++) {
    stringstream keyss;
    stringstream valuess;
    LARGE_INTEGER start, stop;

    GUID guid;
    CoCreateGuid(&guid);

    keyss << "key:" << i << ":" << guid.Data1 << "-" << guid.Data2 << "-" << guid.Data3;
    for (int j = 0; j < 128; j++) {
      valuess << "value" << i+j << endl;
    }

    string sKey = keyss.str();
    string sVal = valuess.str();
    const char *pKey = sKey.c_str();
    int nKey = sKey.length();
    const char *pVal = sVal.c_str();
    int nVal = sVal.length();

    QueryPerformanceCounter(&start);
    while (lsm_insert(db, pKey, nKey, pVal, nVal) == LSM_BUSY) {
      failedAttempts++;
      Sleep(1);
    }
    QueryPerformanceCounter(&stop);
    
    keyLength += nKey;
    valueLength += nVal;
    sum.QuadPart += (stop.QuadPart - start.QuadPart);
  }

  cout << "Failed attempts " << failedAttempts << " !" << endl;

  return double(sum.QuadPart) / freq;
}

void insertionThread(lsm_db *lpParameter)
{
  lsm_db *db = (lsm_db *)lpParameter;
  
  for (int i = 0; i < 10; i++) {
    double timeTaken = runInserts(db, i * 1000, (i + 1) * 1000);
    cout << " Time taken " << timeTaken << "ms" << endl;
  }

  lsm_flush(db);
}

void readerThread(lsm_db *db, int id) {
  for (int i = 0; i < 100; i++) {
    int skip = rand() % 1000;
    lsm_cursor *csr;
    if (lsm_csr_open(db, &csr) != LSM_OK) {
      continue;
    }
    
    Sleep(rand() % 100);

    stringstream keyprefix;
    keyprefix << "key:" << skip;
    string startkey = keyprefix.str();
    int seekrc = lsm_csr_seek(csr, startkey.c_str(), startkey.length() - 1, LSM_SEEK_GE);
    int validrc = lsm_csr_valid(csr);
    if ( seekrc == LSM_OK && validrc) {
      char *pKey, *pVal;
      int nKey, nVal;
      lsm_csr_key(csr, (const void **)&pKey, &nKey);
      lsm_csr_value(csr, (const void **)&pVal, &nVal);

      char *actualKey = new char[nKey + 2];
      memset(actualKey, 0, nKey + 2);
      memcpy(actualKey, pKey, nKey);

      cout << " - " << id << "Found key " << actualKey << endl;
      delete[] actualKey;
    }

    lsm_csr_close(csr);
  }
}

void deleterAll(lsm_db *db)
{
    lsm_cursor *csr;
    if (lsm_csr_open(db, &csr) != LSM_OK) {
        return;
    }
    string firstKey;
    string lastKey;
    char *pFirstKey, *pLastKey;
    int nFirstKey, nLastKey;
    lsm_csr_first(csr);
    int validrc = lsm_csr_valid(csr);
    if (!validrc) {
        lsm_csr_close(csr);
        return;
    }
    lsm_csr_key(csr, (const void **)&pFirstKey, &nFirstKey);
    firstKey.assign(pFirstKey, nFirstKey);
    printf("delete key: %s\n", firstKey.c_str());
    lsm_csr_last(csr);
    lsm_csr_key(csr, (const void **)&pLastKey, &nLastKey);
    lastKey.assign(pLastKey, nLastKey);
    printf("delete key: %s\n", lastKey.c_str());
    lsm_csr_close(csr);
    int rv = lsm_delete_range(db, firstKey.c_str(), nFirstKey, lastKey.c_str(), nLastKey);
    if (rv != LSM_OK) {
        printf("error: %d\n", rv);
    }
    lsm_delete(db, firstKey.c_str(), nFirstKey);
    lsm_delete(db, lastKey.c_str(), nLastKey);
}

int main()
{
  int rc;
  lsm_db *db, *db2;
  int nCkpt = 4 * 1024;             /* 4096KB == 4MB */
  /* Allocate a new database handle */
  rc = lsm_new(0, &db);
  if (rc != LSM_OK) {
    exit(1);
  }
  lsm_config(db, LSM_CONFIG_AUTOCHECKPOINT, &nCkpt);

  /* Connect the database handle to database "test.db" */
  rc = lsm_open(db, "test.lsmdb");
  if (rc != LSM_OK) {
    exit(1);
  }

  /* Allocate a new database handle */
  rc = lsm_new(0, &db2);
  if (rc != LSM_OK) {
    exit(1);
  }

  int ro = 1;
  rc = lsm_config(db2, LSM_CONFIG_READONLY, &ro);

  /* Connect the database handle to database "test.db" */
  rc = lsm_open(db2, "test.lsmdb");
  if (rc != LSM_OK) {
    exit(1);
  }

  thread t1(insertionThread, db);
  thread t2(readerThread, db2, 1);

  t1.join();
  t2.join();

  deleterAll(db);

  deleterAll(db);

  rc = lsm_work(db, 1, -1, 0);

  ::Sleep(5000);

  rc = lsm_close(db2);
  rc = lsm_close(db);
  return 0;
}

