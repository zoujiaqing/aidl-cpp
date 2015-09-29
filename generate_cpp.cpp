/*
 * Copyright (C) 2015, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "generate_cpp.h"
#include "parse_helpers.h"

#include <cctype>
#include <memory>
#include <random>
#include <string>

#include "aidl_language.h"
#include "ast_cpp.h"
#include "code_writer.h"
#include "logging.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace android {
namespace aidl {
namespace cpp {
namespace internals {

string StrippedLiteral(const buffer_type& buffer) {
  std::string i_name = buffer.Literal();

  string c_name;

  if (i_name.length() >= 2 && i_name[0] == 'I' && isupper(i_name[1]))
    c_name = i_name.substr(1);
  else
    c_name = i_name;

  return c_name;
}

string GetCPPType(type_type *type) {
  string lit = type->type.Literal();

  if (lit == "int")
    return "int32_t";
  else if (lit == "long")
    return "int64_t";
  else
    LOG(FATAL) << "Type not yet supported";

  return "int";
}

string GetCPPVarDec(type_type* type, string name,
                    int direction) {
  return GetCPPType(type) + ((OUT_PARAMETER & direction) ? "* " : " ") +
      name + type->Brackets();
}

unique_ptr<Document> BuildClientSource(interface_type* parsed_doc) {
  unique_ptr<CppNamespace> ns{new CppNamespace{"android", {}}};
  return unique_ptr<Document>{new CppSource{ {}, std::move(ns)}};
}

unique_ptr<Document> BuildServerSource(interface_type* parsed_doc) {
  unique_ptr<CppNamespace> ns{new CppNamespace{"android", {}}};
  return unique_ptr<Document>{new CppSource{ {}, std::move(ns)}};
}

unique_ptr<Document> BuildInterfaceSource(interface_type* parsed_doc) {
  unique_ptr<CppNamespace> ns{new CppNamespace{"android", {}}};
  return unique_ptr<Document>{new CppSource{ {}, std::move(ns)}};
}

unique_ptr<Document> BuildClientHeader(interface_type* parsed_doc) {
  string i_name = parsed_doc->name.Literal();
  string c_name = StrippedLiteral(parsed_doc->name);
  string bp_name = "Bp" + c_name;

  unique_ptr<ConstructorDecl> constructor{
        new ConstructorDecl(bp_name, {}, false, false)};

  unique_ptr<ConstructorDecl> destructor{
        new ConstructorDecl("~" + bp_name, {}, false, false)};

  vector<unique_ptr<Declaration>> publics;
  publics.push_back(std::move(constructor));
  publics.push_back(std::move(destructor));

  for (method_type *item = parsed_doc->interface_items;
       item;
       item = item->next) {
    string method_name = item->name.Literal();
    string return_arg = GetCPPVarDec(&item->type, "_aidl_return",
                                     OUT_PARAMETER);

    vector<string> args;

    for (const std::unique_ptr<AidlArgument>& arg : *item->args)
      args.push_back(GetCPPVarDec(&arg->type, arg->name.Literal(),
                                  convert_direction(arg->direction.data)));

    args.push_back(return_arg);

    unique_ptr<Declaration> meth {
        new MethodDecl("android::status_t", method_name,
                       args, false, true)
    };

    publics.push_back(std::move(meth));
  }

  unique_ptr<ClassDecl> bp_class{
      new ClassDecl{bp_name,
                    "public android::BpInterface<" + i_name + ">",
                    std::move(publics),
                    {}
      }};

  vector<unique_ptr<Declaration>> bp_class_vec;
  bp_class_vec.push_back(std::move(bp_class));

  unique_ptr<CppNamespace> ns {new CppNamespace{"android",
                                                std::move(bp_class_vec)}};

  unique_ptr<Document> bp_header{new CppHeader{bp_name + "_H",
                                               {"binder/IBinder.h",
                                                "binder/IInterface.h",
                                                "utils/Errors.h",
                                                i_name + ".h"},
                                               std::move(ns) }};

  return bp_header;
}

unique_ptr<Document> BuildServerHeader(interface_type* parsed_doc) {
  string i_name = parsed_doc->name.Literal();;
  string c_name = StrippedLiteral(parsed_doc->name);
  string bn_name = "Bn" + c_name;

  unique_ptr<Declaration> on_transact{
      new MethodDecl("android::status_t", "onTransact",
                     { "uint32_t code",
                       "const android::Parcel& data",
                       "android::Parcel* reply",
                       "uint32_t flags = 0"
                     })};

  std::vector<unique_ptr<Declaration>> publics;
  publics.push_back(std::move(on_transact));

  unique_ptr<ClassDecl> bn_class{
      new ClassDecl{bn_name,
                    "public android::BnInterface<" + i_name + ">",
                    std::move(publics),
                    {}
      }};

  std::vector<unique_ptr<Declaration>> declarations;
  declarations.push_back(std::move(bn_class));

  unique_ptr<CppNamespace> ns{
      new CppNamespace{"android", std::move(declarations)}};

  unique_ptr<Document> bn_header{new CppHeader{bn_name + "_H",
                                               {"binder/IInterface.h",
                                                i_name + ".h"},
                                               std::move(ns) }};

  return bn_header;
}

unique_ptr<Document> BuildInterfaceHeader(interface_type* parsed_doc) {
  unique_ptr<CppNamespace> ns{new CppNamespace{"android", {}}};
  return unique_ptr<Document>{new CppSource{ {}, std::move(ns)}};
}

bool GenerateCppForFile(const std::string& name, unique_ptr<Document> doc) {
  if (!doc) {
    return false;
  }
  unique_ptr<CodeWriter> writer = GetFileWriter(name);
  doc->Write(writer.get());
  return true;
}

}  // namespace internals

using namespace internals;

bool GenerateCpp(const CppOptions& options, interface_type* parsed_doc) {
  bool success = true;

  success &= GenerateCppForFile(options.ClientCppFileName(),
                                BuildClientSource(parsed_doc));
  success &= GenerateCppForFile(options.ClientHeaderFileName(),
                                BuildClientHeader(parsed_doc));
  success &= GenerateCppForFile(options.ServerCppFileName(),
                                BuildServerSource(parsed_doc));
  success &= GenerateCppForFile(options.ServerHeaderFileName(),
                                BuildServerHeader(parsed_doc));
  success &= GenerateCppForFile(options.InterfaceCppFileName(),
                                BuildInterfaceSource(parsed_doc));
  success &= GenerateCppForFile(options.InterfaceHeaderFileName(),
                                BuildInterfaceHeader(parsed_doc));

  return success;
}

}  // namespace cpp
}  // namespace aidl
}  // namespace android
