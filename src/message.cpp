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

#include "./arrow_types.h"

#if defined(ARROW_R_WITH_ARROW)

// [[arrow::export]]
int64_t ipc___Message__body_length(const std::unique_ptr<arrow::ipc::Message>& message) {
  return message->body_length();
}

// [[arrow::export]]
std::shared_ptr<arrow::Buffer> ipc___Message__metadata(
    const std::unique_ptr<arrow::ipc::Message>& message) {
  return message->metadata();
}

// [[arrow::export]]
std::shared_ptr<arrow::Buffer> ipc___Message__body(
    const std::unique_ptr<arrow::ipc::Message>& message) {
  return message->body();
}

// [[arrow::export]]
int64_t ipc___Message__Verify(const std::unique_ptr<arrow::ipc::Message>& message) {
  return message->Verify();
}

// [[arrow::export]]
arrow::ipc::Message::Type ipc___Message__type(
    const std::unique_ptr<arrow::ipc::Message>& message) {
  return message->type();
}

// [[arrow::export]]
bool ipc___Message__Equals(const std::unique_ptr<arrow::ipc::Message>& x,
                           const std::unique_ptr<arrow::ipc::Message>& y) {
  return x->Equals(*y);
}

// [[arrow::export]]
std::shared_ptr<arrow::RecordBatch> ipc___ReadRecordBatch__Message__Schema(
    const std::unique_ptr<arrow::ipc::Message>& message,
    const std::shared_ptr<arrow::Schema>& schema) {
  std::shared_ptr<arrow::RecordBatch> batch;

  // TODO: perhaps this should come from the R side
  arrow::ipc::DictionaryMemo memo;
  STOP_IF_NOT_OK(arrow::ipc::ReadRecordBatch(*message, schema, &memo, &batch));
  return batch;
}

// [[arrow::export]]
std::shared_ptr<arrow::Schema> ipc___ReadSchema_InputStream(
    const std::shared_ptr<arrow::io::InputStream>& stream) {
  std::shared_ptr<arrow::Schema> schema;
  // TODO: promote to function argument
  arrow::ipc::DictionaryMemo memo;
  STOP_IF_NOT_OK(arrow::ipc::ReadSchema(stream.get(), &memo, &schema));
  return schema;
}

// [[arrow::export]]
std::shared_ptr<arrow::Schema> ipc___ReadSchema_Message(
    const std::unique_ptr<arrow::ipc::Message>& message) {
  std::shared_ptr<arrow::Schema> schema;
  arrow::ipc::DictionaryMemo empty_memo;
  STOP_IF_NOT_OK(arrow::ipc::ReadSchema(*message, &empty_memo, &schema));
  return schema;
}

//--------- MessageReader

// [[arrow::export]]
std::unique_ptr<arrow::ipc::MessageReader> ipc___MessageReader__Open(
    const std::shared_ptr<arrow::io::InputStream>& stream) {
  return arrow::ipc::MessageReader::Open(stream);
}

// [[arrow::export]]
std::unique_ptr<arrow::ipc::Message> ipc___MessageReader__ReadNextMessage(
    const std::unique_ptr<arrow::ipc::MessageReader>& reader) {
  std::unique_ptr<arrow::ipc::Message> message;
  STOP_IF_NOT_OK(reader->ReadNextMessage(&message));
  return message;
}

// [[arrow::export]]
std::unique_ptr<arrow::ipc::Message> ipc___ReadMessage(
    const std::shared_ptr<arrow::io::InputStream>& stream) {
  std::unique_ptr<arrow::ipc::Message> message;
  STOP_IF_NOT_OK(arrow::ipc::ReadMessage(stream.get(), &message));
  return message;
}

#endif
