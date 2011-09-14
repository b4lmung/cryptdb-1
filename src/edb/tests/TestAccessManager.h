/*
 * TestAccessManager.h
 *
 * Created on: July 21, 2011
 *  Author: cat_red
 */

#include <edb/AccessManager.h>
#include <edb/tests/test_utils.h>

#ifndef TESTACCESSMANAGER_H_
#define TESTACCESSMANAGER

class TestAccessManager {
 public:
  TestAccessManager();
  virtual
    ~TestAccessManager();

  static void run(const TestConfig &tc, int argc, char ** argv);
};

#endif /* TESTACCESSMANAGER */
