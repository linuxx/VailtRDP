#pragma once

class QSqlDatabase;

class MigrationManager {
 public:
  bool applyInitialSchema(QSqlDatabase& database) const;
};
