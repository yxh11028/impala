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

#ifndef IMPALA_EXEC_PARQUET_COLUMN_STATS_INLINE_H
#define IMPALA_EXEC_PARQUET_COLUMN_STATS_INLINE_H

#include "exec/parquet-common.h"
#include "parquet-column-stats.h"
#include "runtime/string-value.inline.h"

namespace impala {

template <typename T>
inline void ColumnStats<T>::Update(const T& v) {
  if (!has_values_) {
    has_values_ = true;
    min_value_ = v;
    max_value_ = v;
  } else {
    min_value_ = std::min(min_value_, v);
    max_value_ = std::max(max_value_, v);
  }
}

template <typename T>
inline void ColumnStats<T>::Merge(const ColumnStatsBase& other) {
  DCHECK(dynamic_cast<const ColumnStats<T>*>(&other));
  const ColumnStats<T>* cs = static_cast<const ColumnStats<T>*>(&other);
  if (!cs->has_values_) return;
  if (!has_values_) {
    has_values_ = true;
    min_value_ = cs->min_value_;
    max_value_ = cs->max_value_;
  } else {
    min_value_ = std::min(min_value_, cs->min_value_);
    max_value_ = std::max(max_value_, cs->max_value_);
  }
}

template <typename T>
inline int64_t ColumnStats<T>::BytesNeeded() const {
  return BytesNeeded(min_value_) + BytesNeeded(max_value_);
}

template <typename T>
inline void ColumnStats<T>::EncodeToThrift(parquet::Statistics* out) const {
  DCHECK(has_values_);
  std::string min_str;
  EncodePlainValue(min_value_, BytesNeeded(min_value_), &min_str);
  out->__set_min_value(move(min_str));
  std::string max_str;
  EncodePlainValue(max_value_, BytesNeeded(max_value_), &max_str);
  out->__set_max_value(move(max_str));
}

template <typename T>
inline void ColumnStats<T>::EncodePlainValue(
    const T& v, int64_t bytes_needed, std::string* out) {
  out->resize(bytes_needed);
  int64_t bytes_written = ParquetPlainEncoder::Encode(
      v, bytes_needed, reinterpret_cast<uint8_t*>(&(*out)[0]));
  DCHECK_EQ(bytes_needed, bytes_written);
}

template <typename T>
inline bool ColumnStats<T>::DecodePlainValue(const std::string& buffer, void* slot) {
  T* result = reinterpret_cast<T*>(slot);
  int size = buffer.size();
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&buffer[0]);
  if (ParquetPlainEncoder::Decode(data, data + size, size, result) == -1) return false;
  return true;
}

template <typename T>
inline int64_t ColumnStats<T>::BytesNeeded(const T& v) const {
  return plain_encoded_value_size_ < 0 ? ParquetPlainEncoder::ByteSize<T>(v) :
      plain_encoded_value_size_;
}

/// Plain encoding for Boolean values is not handled by the ParquetPlainEncoder and thus
/// needs special handling here.
template <>
inline void ColumnStats<bool>::EncodePlainValue(
    const bool& v, int64_t bytes_needed, std::string* out) {
  char c = v;
  out->assign(1, c);
}

template <>
inline bool ColumnStats<bool>::DecodePlainValue(const std::string& buffer, void* slot) {
  bool* result = reinterpret_cast<bool*>(slot);
  DCHECK(buffer.size() == 1);
  *result = (buffer[0] != 0);
  return true;
}

template <>
inline int64_t ColumnStats<bool>::BytesNeeded(const bool& v) const {
  return 1;
}

/// Timestamp values need validation.
template <>
inline bool ColumnStats<TimestampValue>::DecodePlainValue(
    const std::string& buffer, void* slot) {
  TimestampValue* result = reinterpret_cast<TimestampValue*>(slot);
  int size = buffer.size();
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&buffer[0]);
  if (ParquetPlainEncoder::Decode(data, data + size, size, result) == -1) return false;
  // We don't need to convert the value here, since we don't support reading timestamp
  // statistics written by Hive / old versions of parquet-mr. Should Hive add support for
  // writing new statistics for the deprecated timestamp type, we will have to add support
  // for conversion here.
  return result->IsValidDate();
}

/// parquet::Statistics stores string values directly and does not use plain encoding.
template <>
inline void ColumnStats<StringValue>::EncodePlainValue(
    const StringValue& v, int64_t bytes_needed, string* out) {
  out->assign(v.ptr, v.len);
}

template <>
inline bool ColumnStats<StringValue>::DecodePlainValue(
    const std::string& buffer, void* slot) {
  StringValue* result = reinterpret_cast<StringValue*>(slot);
  result->ptr = const_cast<char*>(&buffer[0]);
  result->len = buffer.size();
  return true;
}

template <>
inline void ColumnStats<StringValue>::Update(const StringValue& v) {
  if (!has_values_) {
    has_values_ = true;
    min_value_ = v;
    min_buffer_.Clear();
    max_value_ = v;
    max_buffer_.Clear();
  } else {
    if (v < min_value_) {
      min_value_ = v;
      min_buffer_.Clear();
    }
    if (v > max_value_) {
      max_value_ = v;
      max_buffer_.Clear();
    }
  }
}

template <>
inline void ColumnStats<StringValue>::Merge(const ColumnStatsBase& other) {
  DCHECK(dynamic_cast<const ColumnStats<StringValue>*>(&other));
  const ColumnStats<StringValue>* cs =
      static_cast<const ColumnStats<StringValue>*>(&other);
  if (!cs->has_values_) return;
  if (!has_values_) {
    has_values_ = true;
    min_value_ = cs->min_value_;
    min_buffer_.Clear();
    max_value_ = cs->max_value_;
    max_buffer_.Clear();
  } else {
    if (cs->min_value_ < min_value_) {
      min_value_ = cs->min_value_;
      min_buffer_.Clear();
    }
    if (cs->max_value_ > max_value_) {
      max_value_ = cs->max_value_;
      max_buffer_.Clear();
    }
  }
}

// StringValues need to be copied at the end of processing a row batch, since the batch
// memory will be released.
template <>
inline void ColumnStats<StringValue>::MaterializeStringValuesToInternalBuffers() {
  if (min_buffer_.IsEmpty()) CopyToBuffer(&min_buffer_, &min_value_);
  if (max_buffer_.IsEmpty()) CopyToBuffer(&max_buffer_, &max_value_);
}

} // end ns impala
#endif