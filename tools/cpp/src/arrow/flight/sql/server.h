// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Interfaces to use for defining Flight RPC servers. API should be considered
// experimental for now

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "arrow/flight/server.h"
#include "arrow/flight/sql/server.h"
#include "arrow/flight/sql/types.h"
#include "arrow/util/optional.h"

namespace arrow {
namespace flight {
namespace sql {

struct StatementQuery {
  std::string query;
};

struct StatementUpdate {
  std::string query;
};

struct StatementQueryTicket {
  std::string statement_handle;
};

struct PreparedStatementQuery {
  std::string prepared_statement_handle;
};

struct PreparedStatementUpdate {
  std::string prepared_statement_handle;
};

struct GetSqlInfo {
  std::vector<int32_t> info;
};

struct GetDbSchemas {
  util::optional<std::string> catalog;
  util::optional<std::string> db_schema_filter_pattern;
};

struct GetTables {
  util::optional<std::string> catalog;
  util::optional<std::string> db_schema_filter_pattern;
  util::optional<std::string> table_name_filter_pattern;
  std::vector<std::string> table_types;
  bool include_schema;
};

struct GetPrimaryKeys {
  TableRef table_ref;
};

struct GetExportedKeys {
  TableRef table_ref;
};

struct GetImportedKeys {
  TableRef table_ref;
};

struct GetCrossReference {
  TableRef pk_table_ref;
  TableRef fk_table_ref;
};

struct ActionCreatePreparedStatementRequest {
  std::string query;
};

struct ActionClosePreparedStatementRequest {
  std::string prepared_statement_handle;
};

struct ActionCreatePreparedStatementResult {
  std::shared_ptr<Schema> dataset_schema;
  std::shared_ptr<Schema> parameter_schema;
  std::string prepared_statement_handle;
};

/// \brief A utility function to create a ticket (a opaque binary token that the server
///        uses to identify this query) for a statement query.
///        Intended for Flight SQL server implementations.
/// \param[in] statement_handle      The statement handle that will originate the ticket.
/// \return                          The parsed ticket as an string.
arrow::Result<std::string> CreateStatementQueryTicket(
    const std::string& statement_handle);

class ARROW_EXPORT FlightSqlServerBase : public FlightServerBase {
 private:
  SqlInfoResultMap sql_info_id_to_result_;

 public:
  Status GetFlightInfo(const ServerCallContext& context, const FlightDescriptor& request,
                       std::unique_ptr<FlightInfo>* info) override;

  Status DoGet(const ServerCallContext& context, const Ticket& request,
               std::unique_ptr<FlightDataStream>* stream) override;

  Status DoPut(const ServerCallContext& context,
               std::unique_ptr<FlightMessageReader> reader,
               std::unique_ptr<FlightMetadataWriter> writer) override;

  const ActionType kCreatePreparedStatementActionType =
      ActionType{"CreatePreparedStatement",
                 "Creates a reusable prepared statement resource on the server.\n"
                 "Request Message: ActionCreatePreparedStatementRequest\n"
                 "Response Message: ActionCreatePreparedStatementResult"};
  const ActionType kClosePreparedStatementActionType =
      ActionType{"ClosePreparedStatement",
                 "Closes a reusable prepared statement resource on the server.\n"
                 "Request Message: ActionClosePreparedStatementRequest\n"
                 "Response Message: N/A"};

  Status ListActions(const ServerCallContext& context,
                     std::vector<ActionType>* actions) override;

  Status DoAction(const ServerCallContext& context, const Action& action,
                  std::unique_ptr<ResultStream>* result) override;

  /// \brief Get a FlightInfo for executing a SQL query.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The StatementQuery object containing the SQL statement.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoStatement(
      const ServerCallContext& context, const StatementQuery& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the query results.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The StatementQueryTicket containing the statement handle.
  /// \return                 The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetStatement(
      const ServerCallContext& context, const StatementQueryTicket& command);

  /// \brief Get a FlightInfo for executing an already created prepared statement.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The PreparedStatementQuery object containing the
  ///                         prepared statement handle.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoPreparedStatement(
      const ServerCallContext& context, const PreparedStatementQuery& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the prepared statement query results.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The PreparedStatementQuery object containing the
  ///                         prepared statement handle.
  /// \return                 The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetPreparedStatement(
      const ServerCallContext& context, const PreparedStatementQuery& command);

  /// \brief Get a FlightInfo for listing catalogs.
  /// \param[in] context      Per-call context.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoCatalogs(
      const ServerCallContext& context, const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the list of catalogs.
  /// \param[in] context  Per-call context.
  /// \return             An interface for sending data back to the client.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetCatalogs(
      const ServerCallContext& context);

  /// \brief Get a FlightInfo for retrieving other information (See SqlInfo).
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetSqlInfo object containing the list of SqlInfo
  ///                         to be returned.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoSqlInfo(
      const ServerCallContext& context, const GetSqlInfo& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the list of SqlInfo results.
  /// \param[in] context    Per-call context.
  /// \param[in] command    The GetSqlInfo object containing the list of SqlInfo
  ///                       to be returned.
  /// \return               The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetSqlInfo(
      const ServerCallContext& context, const GetSqlInfo& command);

  /// \brief Get a FlightInfo for listing schemas.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetDbSchemas object which may contain filters for
  ///                         catalog and schema name.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoSchemas(
      const ServerCallContext& context, const GetDbSchemas& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the list of schemas.
  /// \param[in] context   Per-call context.
  /// \param[in] command   The GetDbSchemas object which may contain filters for
  ///                      catalog and schema name.
  /// \return              The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetDbSchemas(
      const ServerCallContext& context, const GetDbSchemas& command);

  ///\brief Get a FlightInfo for listing tables.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetTables object which may contain filters for
  ///                         catalog, schema and table names.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoTables(
      const ServerCallContext& context, const GetTables& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the list of tables.
  /// \param[in] context   Per-call context.
  /// \param[in] command   The GetTables object which may contain filters for
  ///                      catalog, schema and table names.
  /// \return              The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetTables(
      const ServerCallContext& context, const GetTables& command);

  /// \brief Get a FlightInfo to extract information about the table types.
  /// \param[in] context      Per-call context.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoTableTypes(
      const ServerCallContext& context, const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the data related to the table types.
  /// \param[in] context   Per-call context.
  /// \return              The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetTableTypes(
      const ServerCallContext& context);

  /// \brief Get a FlightInfo to extract information about primary and foreign keys.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetPrimaryKeys object with necessary information
  ///                         to execute the request.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoPrimaryKeys(
      const ServerCallContext& context, const GetPrimaryKeys& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the data related to the primary and
  /// foreign
  ///        keys.
  /// \param[in] context  Per-call context.
  /// \param[in] command  The GetPrimaryKeys object with necessary information
  ///                     to execute the request.
  /// \return             The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetPrimaryKeys(
      const ServerCallContext& context, const GetPrimaryKeys& command);

  /// \brief Get a FlightInfo to extract information about foreign and primary keys.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetExportedKeys object with necessary information
  ///                         to execute the request.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoExportedKeys(
      const ServerCallContext& context, const GetExportedKeys& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the data related to the foreign and
  /// primary
  ///        keys.
  /// \param[in] context  Per-call context.
  /// \param[in] command  The GetExportedKeys object with necessary information
  ///                     to execute the request.
  /// \return             The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetExportedKeys(
      const ServerCallContext& context, const GetExportedKeys& command);

  /// \brief Get a FlightInfo to extract information about foreign and primary keys.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetImportedKeys object with necessary information
  ///                         to execute the request.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoImportedKeys(
      const ServerCallContext& context, const GetImportedKeys& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the data related to the foreign and
  ///        primary keys.
  /// \param[in] context  Per-call context.
  /// \param[in] command  The GetImportedKeys object with necessary information
  ///                     to execute the request.
  /// \return             The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetImportedKeys(
      const ServerCallContext& context, const GetImportedKeys& command);

  /// \brief Get a FlightInfo to extract information about foreign and primary keys.
  /// \param[in] context      Per-call context.
  /// \param[in] command      The GetCrossReference object with necessary
  /// information
  ///                         to execute the request.
  /// \param[in] descriptor   The descriptor identifying the data stream.
  /// \return                 The FlightInfo describing where to access the
  ///                         dataset.
  virtual arrow::Result<std::unique_ptr<FlightInfo>> GetFlightInfoCrossReference(
      const ServerCallContext& context, const GetCrossReference& command,
      const FlightDescriptor& descriptor);

  /// \brief Get a FlightDataStream containing the data related to the foreign and
  ///        primary keys.
  /// \param[in] context  Per-call context.
  /// \param[in] command  The GetCrossReference object with necessary information
  ///                     to execute the request.
  /// \return             The FlightDataStream containing the results.
  virtual arrow::Result<std::unique_ptr<FlightDataStream>> DoGetCrossReference(
      const ServerCallContext& context, const GetCrossReference& command);

  /// \brief Execute an update SQL statement.
  /// \param[in] context  The call context.
  /// \param[in] command  The StatementUpdate object containing the SQL statement.
  /// \return             The changed record count.
  virtual arrow::Result<int64_t> DoPutCommandStatementUpdate(
      const ServerCallContext& context, const StatementUpdate& command);

  /// \brief Create a prepared statement from given SQL statement.
  /// \param[in] context  The call context.
  /// \param[in] request  The ActionCreatePreparedStatementRequest object containing the
  ///                     SQL statement.
  /// \return             A ActionCreatePreparedStatementResult containing the dataset
  ///                     and parameter schemas and a handle for created statement.
  virtual arrow::Result<ActionCreatePreparedStatementResult> CreatePreparedStatement(
      const ServerCallContext& context,
      const ActionCreatePreparedStatementRequest& request);

  /// \brief Close a prepared statement.
  /// \param[in] context  The call context.
  /// \param[in] request  The ActionClosePreparedStatementRequest object containing the
  ///                     prepared statement handle.
  virtual Status ClosePreparedStatement(
      const ServerCallContext& context,
      const ActionClosePreparedStatementRequest& request);

  /// \brief Bind parameters to given prepared statement.
  /// \param[in] context  The call context.
  /// \param[in] command  The PreparedStatementQuery object containing the
  ///                     prepared statement handle.
  /// \param[in] reader   A sequence of uploaded record batches.
  /// \param[in] writer   Send metadata back to the client.
  virtual Status DoPutPreparedStatementQuery(const ServerCallContext& context,
                                             const PreparedStatementQuery& command,
                                             FlightMessageReader* reader,
                                             FlightMetadataWriter* writer);

  /// \brief Execute an update SQL prepared statement.
  /// \param[in] context  The call context.
  /// \param[in] command  The PreparedStatementUpdate object containing the
  ///                     prepared statement handle.
  /// \param[in] reader   a sequence of uploaded record batches.
  /// \return             The changed record count.
  virtual arrow::Result<int64_t> DoPutPreparedStatementUpdate(
      const ServerCallContext& context, const PreparedStatementUpdate& command,
      FlightMessageReader* reader);

  /// \brief Register a new SqlInfo result, making it available when calling GetSqlInfo.
  /// \param[in] id the SqlInfo identifier.
  /// \param[in] result the result.
  void RegisterSqlInfo(int32_t id, const SqlInfoResult& result);
};

/// \brief Auxiliary class containing all Schemas used on Flight SQL.
class ARROW_EXPORT SqlSchema {
 public:
  /// \brief Get the Schema used on GetCatalogs response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetCatalogsSchema();

  /// \brief Get the Schema used on GetDbSchemas response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetDbSchemasSchema();

  /// \brief Get the Schema used on GetTables response when included schema
  /// flags is set to false.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetTablesSchema();

  /// \brief Get the Schema used on GetTables response when included schema
  /// flags is set to true.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetTablesSchemaWithIncludedSchema();

  /// \brief Get the Schema used on GetTableTypes response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetTableTypesSchema();

  /// \brief Get the Schema used on GetPrimaryKeys response when included schema
  /// flags is set to true.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetPrimaryKeysSchema();

  /// \brief Get the Schema used on GetImportedKeys response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetExportedKeysSchema();

  /// \brief Get the Schema used on GetImportedKeys response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetImportedKeysSchema();

  /// \brief Get the Schema used on GetCrossReference response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetCrossReferenceSchema();

  /// \brief Get the Schema used on GetSqlInfo response.
  /// \return The default schema template.
  static std::shared_ptr<Schema> GetSqlInfoSchema();
};
}  // namespace sql
}  // namespace flight
}  // namespace arrow
