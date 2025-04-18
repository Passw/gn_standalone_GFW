// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_JSON_JSON_VALUE_CONVERTER_H_
#define BASE_JSON_JSON_VALUE_CONVERTER_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"

// JSONValueConverter converts a JSON value into a C++ struct in a
// lightweight way.
//
// Usage:
// For real examples, you may want to refer to _unittest.cc file.
//
// Assume that you have a struct like this:
//   struct Message {
//     int foo;
//     std::string bar;
//     static void RegisterJSONConverter(
//         JSONValueConverter<Message>* converter);
//   };
//
// And you want to parse a json data into this struct.  First, you
// need to declare RegisterJSONConverter() method in your struct.
//   // static
//   void Message::RegisterJSONConverter(
//       JSONValueConverter<Message>* converter) {
//     converter->RegisterIntField("foo", &Message::foo);
//     converter->RegisterStringField("bar", &Message::bar);
//   }
//
// Then, you just instantiate your JSONValueConverter of your type and call
// Convert() method.
//   Message message;
//   JSONValueConverter<Message> converter;
//   converter.Convert(json, &message);
//
// Convert() returns false when it fails.  Here "fail" means that the value is
// structurally different from expected, such like a string value appears
// for an int field.  Do not report failures for missing fields.
// Also note that Convert() will modify the passed |message| even when it
// fails for performance reason.
//
// For nested field, the internal message also has to implement the registration
// method.  Then, just use RegisterNestedField() from the containing struct's
// RegisterJSONConverter method.
//   struct Nested {
//     Message foo;
//     static void RegisterJSONConverter(...) {
//       ...
//       converter->RegisterNestedField("foo", &Nested::foo);
//     }
//   };
//
// For repeated field, we just assume std::vector<std::unique_ptr<ElementType>>
// for its container and you can put RegisterRepeatedInt or some other types.
// Use RegisterRepeatedMessage for nested repeated fields.
//
// Sometimes JSON format uses string representations for other types such
// like enum, timestamp, or URL.  You can use RegisterCustomField method
// and specify a function to convert a std::string_view to your type.
//   bool ConvertFunc(std::string_view s, YourEnum* result) {
//     // do something and return true if succeed...
//   }
//   struct Message {
//     YourEnum ye;
//     ...
//     static void RegisterJSONConverter(...) {
//       ...
//       converter->RegsiterCustomField<YourEnum>(
//           "your_enum", &Message::ye, &ConvertFunc);
//     }
//   };

namespace base {

template <typename StructType>
class JSONValueConverter;

namespace internal {

template <typename StructType>
class FieldConverterBase {
 public:
  explicit FieldConverterBase(const std::string& path) : field_path_(path) {}
  virtual ~FieldConverterBase() = default;
  virtual bool ConvertField(const base::Value& value,
                            StructType* obj) const = 0;
  const std::string& field_path() const { return field_path_; }

 private:
  std::string field_path_;
  FieldConverterBase(const FieldConverterBase&) = delete;
  FieldConverterBase& operator=(const FieldConverterBase&) = delete;
};

template <typename FieldType>
class ValueConverter {
 public:
  virtual ~ValueConverter() = default;
  virtual bool Convert(const base::Value& value, FieldType* field) const = 0;
};

template <typename StructType, typename FieldType>
class FieldConverter : public FieldConverterBase<StructType> {
 public:
  explicit FieldConverter(const std::string& path,
                          FieldType StructType::* field,
                          ValueConverter<FieldType>* converter)
      : FieldConverterBase<StructType>(path),
        field_pointer_(field),
        value_converter_(converter) {}

  bool ConvertField(const base::Value& value, StructType* dst) const override {
    return value_converter_->Convert(value, &(dst->*field_pointer_));
  }

 private:
  FieldType StructType::* field_pointer_;
  std::unique_ptr<ValueConverter<FieldType>> value_converter_;
  FieldConverter(const FieldConverter&) = delete;
  FieldConverter& operator=(const FieldConverter&) = delete;
};

template <typename FieldType>
class BasicValueConverter;

template <>
class BasicValueConverter<int> : public ValueConverter<int> {
 public:
  BasicValueConverter() = default;

  bool Convert(const base::Value& value, int* field) const override;

 private:
  BasicValueConverter(const BasicValueConverter&) = delete;
  BasicValueConverter& operator=(const BasicValueConverter&) = delete;
};

template <>
class BasicValueConverter<std::string> : public ValueConverter<std::string> {
 public:
  BasicValueConverter() = default;

  bool Convert(const base::Value& value, std::string* field) const override;

 private:
  BasicValueConverter(const BasicValueConverter&) = delete;
  BasicValueConverter& operator=(const BasicValueConverter&) = delete;
};

template <>
class BasicValueConverter<std::u16string>
    : public ValueConverter<std::u16string> {
 public:
  BasicValueConverter() = default;

  bool Convert(const base::Value& value, std::u16string* field) const override;

 private:
  BasicValueConverter(const BasicValueConverter&) = delete;
  BasicValueConverter& operator=(const BasicValueConverter&) = delete;
};

template <>
class BasicValueConverter<double> : public ValueConverter<double> {
 public:
  BasicValueConverter() = default;

  bool Convert(const base::Value& value, double* field) const override;

 private:
  BasicValueConverter(const BasicValueConverter&) = delete;
  BasicValueConverter& operator=(const BasicValueConverter&) = delete;
};

template <>
class BasicValueConverter<bool> : public ValueConverter<bool> {
 public:
  BasicValueConverter() = default;

  bool Convert(const base::Value& value, bool* field) const override;

 private:
  BasicValueConverter(const BasicValueConverter&) = delete;
  BasicValueConverter& operator=(const BasicValueConverter&) = delete;
};

template <typename FieldType>
class ValueFieldConverter : public ValueConverter<FieldType> {
 public:
  typedef bool (*ConvertFunc)(const base::Value* value, FieldType* field);

  explicit ValueFieldConverter(ConvertFunc convert_func)
      : convert_func_(convert_func) {}

  bool Convert(const base::Value& value, FieldType* field) const override {
    return convert_func_(&value, field);
  }

 private:
  ConvertFunc convert_func_;

  ValueFieldConverter(const ValueFieldConverter&) = delete;
  ValueFieldConverter& operator=(const ValueFieldConverter&) = delete;
};

template <typename FieldType>
class CustomFieldConverter : public ValueConverter<FieldType> {
 public:
  typedef bool (*ConvertFunc)(std::string_view value, FieldType* field);

  explicit CustomFieldConverter(ConvertFunc convert_func)
      : convert_func_(convert_func) {}

  bool Convert(const base::Value& value, FieldType* field) const override {
    std::string string_value;
    return value.GetAsString(&string_value) &&
           convert_func_(string_value, field);
  }

 private:
  ConvertFunc convert_func_;

  CustomFieldConverter(const CustomFieldConverter&) = delete;
  CustomFieldConverter& operator=(const CustomFieldConverter&) = delete;
};

template <typename NestedType>
class NestedValueConverter : public ValueConverter<NestedType> {
 public:
  NestedValueConverter() = default;

  bool Convert(const base::Value& value, NestedType* field) const override {
    return converter_.Convert(value, field);
  }

 private:
  JSONValueConverter<NestedType> converter_;
  NestedValueConverter(const NestedValueConverter&) = delete;
  NestedValueConverter& operator=(const NestedValueConverter&) = delete;
};

template <typename Element>
class RepeatedValueConverter
    : public ValueConverter<std::vector<std::unique_ptr<Element>>> {
 public:
  RepeatedValueConverter() = default;

  bool Convert(const base::Value& value,
               std::vector<std::unique_ptr<Element>>* field) const override {
    const base::ListValue* list = NULL;
    if (!value.GetAsList(&list)) {
      // The field is not a list.
      return false;
    }

    field->reserve(list->GetSize());
    for (size_t i = 0; i < list->GetSize(); ++i) {
      const base::Value* element = NULL;
      if (!list->Get(i, &element))
        continue;

      std::unique_ptr<Element> e(new Element);
      if (basic_converter_.Convert(*element, e.get())) {
        field->push_back(std::move(e));
      } else {
        return false;
      }
    }
    return true;
  }

 private:
  BasicValueConverter<Element> basic_converter_;
  RepeatedValueConverter(const RepeatedValueConverter&) = delete;
  RepeatedValueConverter& operator=(const RepeatedValueConverter&) = delete;
};

template <typename NestedType>
class RepeatedMessageConverter
    : public ValueConverter<std::vector<std::unique_ptr<NestedType>>> {
 public:
  RepeatedMessageConverter() = default;

  bool Convert(const base::Value& value,
               std::vector<std::unique_ptr<NestedType>>* field) const override {
    const base::ListValue* list = NULL;
    if (!value.GetAsList(&list))
      return false;

    field->reserve(list->GetSize());
    for (size_t i = 0; i < list->GetSize(); ++i) {
      const base::Value* element = NULL;
      if (!list->Get(i, &element))
        continue;

      std::unique_ptr<NestedType> nested(new NestedType);
      if (converter_.Convert(*element, nested.get())) {
        field->push_back(std::move(nested));
      } else {
        return false;
      }
    }
    return true;
  }

 private:
  JSONValueConverter<NestedType> converter_;
  RepeatedMessageConverter(const RepeatedMessageConverter&) = delete;
  RepeatedMessageConverter& operator=(const RepeatedMessageConverter&) = delete;
};

template <typename NestedType>
class RepeatedCustomValueConverter
    : public ValueConverter<std::vector<std::unique_ptr<NestedType>>> {
 public:
  typedef bool (*ConvertFunc)(const base::Value* value, NestedType* field);

  explicit RepeatedCustomValueConverter(ConvertFunc convert_func)
      : convert_func_(convert_func) {}

  bool Convert(const base::Value& value,
               std::vector<std::unique_ptr<NestedType>>* field) const override {
    const base::ListValue* list = NULL;
    if (!value.GetAsList(&list))
      return false;

    field->reserve(list->GetSize());
    for (size_t i = 0; i < list->GetSize(); ++i) {
      const base::Value* element = NULL;
      if (!list->Get(i, &element))
        continue;

      std::unique_ptr<NestedType> nested(new NestedType);
      if ((*convert_func_)(element, nested.get())) {
        field->push_back(std::move(nested));
      } else {
        return false;
      }
    }
    return true;
  }

 private:
  ConvertFunc convert_func_;
  RepeatedCustomValueConverter(const RepeatedCustomValueConverter&) = delete;
  RepeatedCustomValueConverter& operator=(const RepeatedCustomValueConverter&) =
      delete;
};

}  // namespace internal

template <class StructType>
class JSONValueConverter {
 public:
  JSONValueConverter() { StructType::RegisterJSONConverter(this); }

  void RegisterIntField(const std::string& field_name,
                        int StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, int>>(
            field_name, field, new internal::BasicValueConverter<int>));
  }

  void RegisterStringField(const std::string& field_name,
                           std::string StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, std::string>>(
            field_name, field, new internal::BasicValueConverter<std::string>));
  }

  void RegisterStringField(const std::string& field_name,
                           std::u16string StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, std::u16string>>(
            field_name, field,
            new internal::BasicValueConverter<std::u16string>));
  }

  void RegisterBoolField(const std::string& field_name,
                         bool StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, bool>>(
            field_name, field, new internal::BasicValueConverter<bool>));
  }

  void RegisterDoubleField(const std::string& field_name,
                           double StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, double>>(
            field_name, field, new internal::BasicValueConverter<double>));
  }

  template <class NestedType>
  void RegisterNestedField(const std::string& field_name,
                           NestedType StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, NestedType>>(
            field_name, field, new internal::NestedValueConverter<NestedType>));
  }

  template <typename FieldType>
  void RegisterCustomField(const std::string& field_name,
                           FieldType StructType::* field,
                           bool (*convert_func)(std::string_view, FieldType*)) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, FieldType>>(
            field_name, field,
            new internal::CustomFieldConverter<FieldType>(convert_func)));
  }

  template <typename FieldType>
  void RegisterCustomValueField(const std::string& field_name,
                                FieldType StructType::* field,
                                bool (*convert_func)(const base::Value*,
                                                     FieldType*)) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<StructType, FieldType>>(
            field_name, field,
            new internal::ValueFieldConverter<FieldType>(convert_func)));
  }

  void RegisterRepeatedInt(
      const std::string& field_name,
      std::vector<std::unique_ptr<int>> StructType::* field) {
    fields_.push_back(std::make_unique<internal::FieldConverter<
                          StructType, std::vector<std::unique_ptr<int>>>>(
        field_name, field, new internal::RepeatedValueConverter<int>));
  }

  void RegisterRepeatedString(
      const std::string& field_name,
      std::vector<std::unique_ptr<std::string>> StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<
            StructType, std::vector<std::unique_ptr<std::string>>>>(
            field_name, field,
            new internal::RepeatedValueConverter<std::string>));
  }

  void RegisterRepeatedString(
      const std::string& field_name,
      std::vector<std::unique_ptr<std::u16string>> StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<
            StructType, std::vector<std::unique_ptr<std::u16string>>>>(
            field_name, field,
            new internal::RepeatedValueConverter<std::u16string>));
  }

  void RegisterRepeatedDouble(
      const std::string& field_name,
      std::vector<std::unique_ptr<double>> StructType::* field) {
    fields_.push_back(std::make_unique<internal::FieldConverter<
                          StructType, std::vector<std::unique_ptr<double>>>>(
        field_name, field, new internal::RepeatedValueConverter<double>));
  }

  void RegisterRepeatedBool(
      const std::string& field_name,
      std::vector<std::unique_ptr<bool>> StructType::* field) {
    fields_.push_back(std::make_unique<internal::FieldConverter<
                          StructType, std::vector<std::unique_ptr<bool>>>>(
        field_name, field, new internal::RepeatedValueConverter<bool>));
  }

  template <class NestedType>
  void RegisterRepeatedCustomValue(
      const std::string& field_name,
      std::vector<std::unique_ptr<NestedType>> StructType::* field,
      bool (*convert_func)(const base::Value*, NestedType*)) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<
            StructType, std::vector<std::unique_ptr<NestedType>>>>(
            field_name, field,
            new internal::RepeatedCustomValueConverter<NestedType>(
                convert_func)));
  }

  template <class NestedType>
  void RegisterRepeatedMessage(
      const std::string& field_name,
      std::vector<std::unique_ptr<NestedType>> StructType::* field) {
    fields_.push_back(
        std::make_unique<internal::FieldConverter<
            StructType, std::vector<std::unique_ptr<NestedType>>>>(
            field_name, field,
            new internal::RepeatedMessageConverter<NestedType>));
  }

  bool Convert(const base::Value& value, StructType* output) const {
    const DictionaryValue* dictionary_value = NULL;
    if (!value.GetAsDictionary(&dictionary_value))
      return false;

    for (size_t i = 0; i < fields_.size(); ++i) {
      const internal::FieldConverterBase<StructType>* field_converter =
          fields_[i].get();
      const base::Value* field = NULL;
      if (dictionary_value->Get(field_converter->field_path(), &field)) {
        if (!field_converter->ConvertField(*field, output)) {
          return false;
        }
      }
    }
    return true;
  }

 private:
  std::vector<std::unique_ptr<internal::FieldConverterBase<StructType>>>
      fields_;

  JSONValueConverter(const JSONValueConverter&) = delete;
  JSONValueConverter& operator=(const JSONValueConverter&) = delete;
};

}  // namespace base

#endif  // BASE_JSON_JSON_VALUE_CONVERTER_H_
