#pragma once

#include <QString>

class QSqlDatabase;

class DatabaseManager {
 public:
  explicit DatabaseManager(QString databasePath);
  ~DatabaseManager();

  DatabaseManager(const DatabaseManager&) = delete;
  DatabaseManager& operator=(const DatabaseManager&) = delete;

  bool initialize();
  QSqlDatabase database() const;
  QString databasePath() const;

 private:
  QString databasePath_;
  QString connectionName_;
};
