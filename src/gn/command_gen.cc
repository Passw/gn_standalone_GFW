// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <mutex>
#include <thread>
#include <unordered_map>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/timer/elapsed_timer.h"
#include "gn/build_settings.h"
#include "gn/commands.h"
#include "gn/compile_commands_writer.h"
#include "gn/eclipse_writer.h"
#include "gn/filesystem_utils.h"
#include "gn/json_project_writer.h"
#include "gn/label_pattern.h"
#include "gn/ninja_outputs_writer.h"
#include "gn/ninja_target_writer.h"
#include "gn/ninja_tools.h"
#include "gn/ninja_writer.h"
#include "gn/qt_creator_writer.h"
#include "gn/runtime_deps.h"
#include "gn/rust_project_writer.h"
#include "gn/scheduler.h"
#include "gn/setup.h"
#include "gn/standard_out.h"
#include "gn/switches.h"
#include "gn/target.h"
#include "gn/visual_studio_writer.h"
#include "gn/xcode_writer.h"

namespace commands {

namespace {

const char kSwitchCheck[] = "check";
const char kSwitchCleanStale[] = "clean-stale";
const char kSwitchFilters[] = "filters";
const char kSwitchIde[] = "ide";
const char kSwitchIdeValueEclipse[] = "eclipse";
const char kSwitchIdeValueQtCreator[] = "qtcreator";
const char kSwitchIdeValueVs[] = "vs";
const char kSwitchIdeValueVs2013[] = "vs2013";
const char kSwitchIdeValueVs2015[] = "vs2015";
const char kSwitchIdeValueVs2017[] = "vs2017";
const char kSwitchIdeValueVs2019[] = "vs2019";
const char kSwitchIdeValueVs2022[] = "vs2022";
const char kSwitchIdeValueWinSdk[] = "winsdk";
const char kSwitchIdeValueXcode[] = "xcode";
const char kSwitchIdeValueJson[] = "json";
const char kSwitchIdeRootTarget[] = "ide-root-target";
const char kSwitchNinjaExecutable[] = "ninja-executable";
const char kSwitchNinjaExtraArgs[] = "ninja-extra-args";
const char kSwitchNinjaOutputsFile[] = "ninja-outputs-file";
const char kSwitchNinjaOutputsScript[] = "ninja-outputs-script";
const char kSwitchNinjaOutputsScriptArgs[] = "ninja-outputs-script-args";
const char kSwitchNoDeps[] = "no-deps";
const char kSwitchSln[] = "sln";
const char kSwitchXcodeProject[] = "xcode-project";
const char kSwitchXcodeBuildSystem[] = "xcode-build-system";
const char kSwitchXcodeBuildsystemValueLegacy[] = "legacy";
const char kSwitchXcodeBuildsystemValueNew[] = "new";
const char kSwitchXcodeConfigurations[] = "xcode-configs";
const char kSwitchXcodeConfigurationBuildPath[] = "xcode-config-build-dir";
const char kSwitchXcodeAdditionalFilesPatterns[] =
    "xcode-additional-files-patterns";
const char kSwitchXcodeAdditionalFilesRoots[] = "xcode-additional-files-roots";
const char kSwitchJsonFileName[] = "json-file-name";
const char kSwitchJsonIdeScript[] = "json-ide-script";
const char kSwitchJsonIdeScriptArgs[] = "json-ide-script-args";
const char kSwitchExportCompileCommands[] = "export-compile-commands";
const char kSwitchExportRustProject[] = "export-rust-project";
const char kSwitchFilterWithData[] = "filter-with-data";

// A map type used to implement --ide=ninja_outputs
using NinjaOutputsMap = NinjaOutputsWriter::MapType;

// Collects Ninja rules for each toolchain. The lock protects the rules
struct TargetWriteInfo {
  // Set this to true to populate |ninja_outputs_map| below.
  bool want_ninja_outputs = false;

  std::mutex lock;
  NinjaWriter::PerToolchainRules rules;

  NinjaOutputsMap ninja_outputs_map;

  using ResolvedMap = std::unordered_map<std::thread::id, ResolvedTargetData>;
  std::unique_ptr<ResolvedMap> resolved_map = std::make_unique<ResolvedMap>();

  void LeakOnPurpose() { (void)resolved_map.release(); }
};

// Called on worker thread to write the ninja file.
void BackgroundDoWrite(TargetWriteInfo* write_info, const Target* target) {
  ResolvedTargetData* resolved;
  std::vector<OutputFile> target_ninja_outputs;
  std::vector<OutputFile>* ninja_outputs =
      write_info->want_ninja_outputs ? &target_ninja_outputs : nullptr;

  {
    std::lock_guard<std::mutex> lock(write_info->lock);
    resolved = &((*write_info->resolved_map)[std::this_thread::get_id()]);
  }
  std::string rule =
      NinjaTargetWriter::RunAndWriteFile(target, resolved, ninja_outputs);

  {
    std::lock_guard<std::mutex> lock(write_info->lock);
    // Even if rule is empty, add it to the map to ensure a corresponding
    // .toolchain file will be generated, otherwise Ninja will complain
    // when the build.ninja file tries to load a non-existent .toolchain
    // file.
    write_info->rules[target->toolchain()].emplace_back(target,
                                                        std::move(rule));

    if (write_info->want_ninja_outputs) {
      write_info->ninja_outputs_map.emplace(target,
                                            std::move(target_ninja_outputs));
    }
  }
}

// Called on the main thread.
void ItemResolvedAndGeneratedCallback(TargetWriteInfo* write_info,
                                      const BuilderRecord* record) {
  const Item* item = record->item();
  const Target* target = item->AsTarget();
  if (target) {
    g_scheduler->ScheduleWork(
        [write_info, target]() { BackgroundDoWrite(write_info, target); });
  }
}

// Returns a pointer to the target with the given file as an output, or null
// if no targets generate the file. This is brute force since this is an
// error condition and performance shouldn't matter.
const Target* FindTargetThatGeneratesFile(const Builder& builder,
                                          const SourceFile& file) {
  std::vector<const Target*> targets = builder.GetAllResolvedTargets();
  if (targets.empty())
    return nullptr;

  OutputFile output_file(targets[0]->settings()->build_settings(), file);
  for (const Target* target : targets) {
    for (const auto& cur_output : target->computed_outputs()) {
      if (cur_output == output_file)
        return target;
    }
  }
  return nullptr;
}

// Prints an error that the given file was present as a source or input in
// the given target(s) but was not generated by any of its dependencies.
void PrintInvalidGeneratedInput(const Builder& builder,
                                const SourceFile& file,
                                const std::vector<const Target*>& targets) {
  std::string err;

  // Only show the toolchain labels (which can be confusing) if something
  // isn't the default.
  bool show_toolchains = false;
  const Label& default_toolchain =
      targets[0]->settings()->default_toolchain_label();
  for (const Target* target : targets) {
    if (target->settings()->toolchain_label() != default_toolchain) {
      show_toolchains = true;
      break;
    }
  }

  const Target* generator = FindTargetThatGeneratesFile(builder, file);
  if (generator &&
      generator->settings()->toolchain_label() != default_toolchain)
    show_toolchains = true;

  const std::string target_str = targets.size() > 1 ? "targets" : "target";
  err += "The file:\n";
  err += "  " + file.value() + "\n";
  err += "is listed as an input or source for the " + target_str + ":\n";
  for (const Target* target : targets)
    err += "  " + target->label().GetUserVisibleName(show_toolchains) + "\n";

  if (generator) {
    err += "but this file was not generated by any dependencies of the " +
           target_str + ". The target\nthat generates the file is:\n  ";
    err += generator->label().GetUserVisibleName(show_toolchains);
  } else {
    err += "but no targets in the build generate that file.";
  }

  Err(Location(), "Input to " + target_str + " not generated by a dependency.",
      err)
      .PrintToStdout();
}

bool CheckForInvalidGeneratedInputs(Setup* setup) {
  std::multimap<SourceFile, const Target*> unknown_inputs =
      g_scheduler->GetUnknownGeneratedInputs();
  if (unknown_inputs.empty())
    return true;  // No bad files.

  int errors_found = 0;
  auto cur = unknown_inputs.begin();
  while (cur != unknown_inputs.end()) {
    errors_found++;
    auto end_of_range = unknown_inputs.upper_bound(cur->first);

    // Package the values more conveniently for printing.
    SourceFile bad_input = cur->first;
    std::vector<const Target*> targets;
    while (cur != end_of_range)
      targets.push_back((cur++)->second);

    PrintInvalidGeneratedInput(setup->builder(), bad_input, targets);
    OutputString("\n");
  }

  OutputString(
      "If you have generated inputs, there needs to be a dependency path "
      "between the\ntwo targets in addition to just listing the files. For "
      "indirect dependencies,\nthe intermediate ones must be public_deps. "
      "data_deps don't count since they're\nonly runtime dependencies. If "
      "you think a dependency chain exists, it might be\nbecause the chain "
      "is private. Try \"gn path\" to analyze.\n");

  if (errors_found > 1) {
    OutputString(base::StringPrintf("\n%d generated input errors found.\n",
                                    errors_found),
                 DECORATION_YELLOW);
  }
  return false;
}

bool RunIdeWriter(const std::string& ide,
                  const BuildSettings* build_settings,
                  const Builder& builder,
                  Err* err) {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  bool quiet = command_line->HasSwitch(switches::kQuiet);
  base::ElapsedTimer timer;

  if (ide == kSwitchIdeValueEclipse) {
    bool res = EclipseWriter::RunAndWriteFile(build_settings, builder, err);
    if (res && !quiet) {
      OutputString("Generating Eclipse settings took " +
                   base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                   "ms\n");
    }
    return res;
  } else if (ide == kSwitchIdeValueVs || ide == kSwitchIdeValueVs2013 ||
             ide == kSwitchIdeValueVs2015 || ide == kSwitchIdeValueVs2017 ||
             ide == kSwitchIdeValueVs2019 || ide == kSwitchIdeValueVs2022) {
    VisualStudioWriter::Version version = VisualStudioWriter::Version::Vs2022;
    if (ide == kSwitchIdeValueVs2013)
      version = VisualStudioWriter::Version::Vs2013;
    else if (ide == kSwitchIdeValueVs2015)
      version = VisualStudioWriter::Version::Vs2015;
    else if (ide == kSwitchIdeValueVs2017)
      version = VisualStudioWriter::Version::Vs2017;
    else if (ide == kSwitchIdeValueVs2019)
      version = VisualStudioWriter::Version::Vs2019;
    else if (ide == kSwitchIdeValueVs2022)
      version = VisualStudioWriter::Version::Vs2022;

    std::string sln_name;
    if (command_line->HasSwitch(kSwitchSln))
      sln_name = command_line->GetSwitchValueString(kSwitchSln);
    std::string filters;
    if (command_line->HasSwitch(kSwitchFilters))
      filters = command_line->GetSwitchValueString(kSwitchFilters);
    std::string win_kit;
    if (command_line->HasSwitch(kSwitchIdeValueWinSdk))
      win_kit = command_line->GetSwitchValueString(kSwitchIdeValueWinSdk);
    std::string ninja_extra_args;
    if (command_line->HasSwitch(kSwitchNinjaExtraArgs)) {
      ninja_extra_args =
          command_line->GetSwitchValueString(kSwitchNinjaExtraArgs);
    }
    std::string ninja_executable;
    if (command_line->HasSwitch(kSwitchNinjaExecutable)) {
      ninja_executable =
          command_line->GetSwitchValueString(kSwitchNinjaExecutable);
    }
    bool no_deps = command_line->HasSwitch(kSwitchNoDeps);
    bool res = VisualStudioWriter::RunAndWriteFiles(
        build_settings, builder, version, sln_name, filters, win_kit,
        ninja_extra_args, ninja_executable, no_deps, err);
    if (res && !quiet) {
      OutputString("Generating Visual Studio projects took " +
                   base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                   "ms\n");
    }
    return res;
  } else if (ide == kSwitchIdeValueXcode) {
    XcodeWriter::Options options = {
        command_line->GetSwitchValueString(kSwitchXcodeProject),
        command_line->GetSwitchValueString(kSwitchIdeRootTarget),
        command_line->GetSwitchValueString(kSwitchNinjaExecutable),
        command_line->GetSwitchValueString(kSwitchFilters),
        command_line->GetSwitchValueString(kSwitchXcodeConfigurations),
        command_line->GetSwitchValuePath(kSwitchXcodeConfigurationBuildPath),
        command_line->GetSwitchValueNative(kSwitchXcodeAdditionalFilesPatterns),
        command_line->GetSwitchValueNative(kSwitchXcodeAdditionalFilesRoots),
        XcodeBuildSystem::kLegacy,
    };

    if (options.project_name.empty()) {
      options.project_name = "all";
    }

    const std::string build_system =
        command_line->GetSwitchValueString(kSwitchXcodeBuildSystem);
    if (!build_system.empty()) {
      if (build_system == kSwitchXcodeBuildsystemValueNew) {
        options.build_system = XcodeBuildSystem::kNew;
      } else if (build_system == kSwitchXcodeBuildsystemValueLegacy) {
        options.build_system = XcodeBuildSystem::kLegacy;
      } else {
        *err = Err(Location(), "Unknown build system: " + build_system);
        return false;
      }
    }

    bool res =
        XcodeWriter::RunAndWriteFiles(build_settings, builder, options, err);
    if (res && !quiet) {
      OutputString("Generating Xcode projects took " +
                   base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                   "ms\n");
    }
    return res;
  } else if (ide == kSwitchIdeValueQtCreator) {
    std::string root_target;
    if (command_line->HasSwitch(kSwitchIdeRootTarget))
      root_target = command_line->GetSwitchValueString(kSwitchIdeRootTarget);
    bool res = QtCreatorWriter::RunAndWriteFile(build_settings, builder, err,
                                                root_target);
    if (res && !quiet) {
      OutputString("Generating QtCreator projects took " +
                   base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                   "ms\n");
    }
    return res;
  } else if (ide == kSwitchIdeValueJson) {
    std::string file_name =
        command_line->GetSwitchValueString(kSwitchJsonFileName);
    if (file_name.empty())
      file_name = "project.json";
    std::string exec_script =
        command_line->GetSwitchValueString(kSwitchJsonIdeScript);
    std::string exec_script_extra_args =
        command_line->GetSwitchValueString(kSwitchJsonIdeScriptArgs);
    std::string filters = command_line->GetSwitchValueString(kSwitchFilters);
    bool filter_with_data = command_line->HasSwitch(kSwitchFilterWithData);

    bool res = JSONProjectWriter::RunAndWriteFiles(
        build_settings, builder, file_name, exec_script, exec_script_extra_args,
        filters, filter_with_data, quiet, err);
    if (res && !quiet) {
      OutputString("Generating JSON projects took " +
                   base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                   "ms\n");
    }
    return res;
  }

  *err = Err(Location(), "Unknown IDE: " + ide);
  return false;
}

bool RunRustProjectWriter(const BuildSettings* build_settings,
                          const Builder& builder,
                          Err* err) {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  bool quiet = command_line->HasSwitch(switches::kQuiet);
  base::ElapsedTimer timer;

  std::string file_name = "rust-project.json";
  bool res = RustProjectWriter::RunAndWriteFiles(build_settings, builder,
                                                 file_name, quiet, err);
  if (res && !quiet) {
    OutputString("Generating rust-project.json took " +
                 base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                 "ms\n");
  }
  return res;
}

bool RunCompileCommandsWriter(Setup& setup, Err* err) {
  // The compilation database is written if either the .gn setting is set or if
  // the command line flag is set. The command line flag takes precedence.
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  bool has_legacy_switch =
      command_line->HasSwitch(kSwitchExportCompileCommands);

  bool has_patterns = !setup.export_compile_commands().empty();
  if (!has_legacy_switch && !has_patterns)
    return true;  // No compilation database needs to be written.

  bool quiet = command_line->HasSwitch(switches::kQuiet);
  base::ElapsedTimer timer;

  // The compilation database file goes in the build directory.
  SourceFile output_file =
      setup.build_settings().build_dir().ResolveRelativeFile(
          Value(nullptr, "compile_commands.json"), err);
  if (output_file.is_null())
    return false;
  base::FilePath output_path = setup.build_settings().GetFullPath(output_file);

  std::optional<std::string> legacy_target_filters;
  if (has_legacy_switch) {
    legacy_target_filters =
        command_line->GetSwitchValueString(kSwitchExportCompileCommands);
  }

  bool ok = CompileCommandsWriter::RunAndWriteFiles(
      &setup.build_settings(), setup.builder().GetAllResolvedTargets(),
      setup.export_compile_commands(), legacy_target_filters, output_path, err);
  if (ok && !quiet) {
    OutputString("Generating compile_commands took " +
                 base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                 "ms\n");
  }
  return ok;
}

bool RunNinjaPostProcessTools(const BuildSettings* build_settings,
                              base::FilePath ninja_executable,
                              bool is_regeneration,
                              bool clean_stale,
                              Err* err) {
  // If the user did not specify an executable, skip running the post processing
  // tools. Since these tools can re-write ninja build log and dep logs, it is
  // really important that ninja executable used for tools matches the
  // executable that is used for builds.
  if (ninja_executable.empty()) {
    if (clean_stale) {
      *err = Err(Location(), "No --ninja-executable provided.",
                 "--clean-stale requires a ninja executable to run. You can "
                 "provide one on the command line via --ninja-executable.");
      return false;
    }

    return true;
  }

  base::FilePath build_dir =
      build_settings->GetFullPath(build_settings->build_dir());

  if (clean_stale) {
    if (build_settings->ninja_required_version() < Version{1, 10, 0}) {
      *err = Err(Location(), "Need a ninja executable at least version 1.10.0.",
                 "--clean-stale requires a ninja executable of version 1.10.0 "
                 "or later.");
      return false;
    }

    if (!InvokeNinjaCleanDeadTool(ninja_executable, build_dir, err)) {
      return false;
    }

    if (!InvokeNinjaRecompactTool(ninja_executable, build_dir, err)) {
      return false;
    }
  }

  // If we have a ninja version that supports restat, we should restat the
  // build.ninja or build.ninja.stamp files so the next ninja invocation
  // will use the right mtimes. If gen is being invoked as part of a re-gen
  // (ie, ninja is invoking gn gen), then we can elide this restat, as
  // ninja will restat the appropriate file anyways after it is complete.
  if (!is_regeneration &&
      build_settings->ninja_required_version() >= Version{1, 10, 0}) {
    std::vector<base::FilePath> files_to_restat{
        base::FilePath(FILE_PATH_LITERAL("build.ninja")),
        base::FilePath(FILE_PATH_LITERAL("build.ninja.stamp")),
    };
    if (!InvokeNinjaRestatTool(ninja_executable, build_dir, files_to_restat,
                               err)) {
      return false;
    }
  }
  return true;
}

bool WriteIgnoreFile(Setup& setup, Err* err) {
  // Write a .gitignore file that causes the build directory to be ignored.
  base::FilePath output_path =
      setup.build_settings()
          .GetFullPath(setup.build_settings().build_dir())
          .Append(FILE_PATH_LITERAL(".gitignore"));

  if (base::PathExists(output_path))
    return true;

  return WriteFile(output_path, "# Created by GN\n*\n", err);
}

}  // namespace

const char kGen[] = "gen";
const char kGen_HelpShort[] = "gen: Generate ninja files.";
const char kGen_Help[] =
    R"(gn gen [--check] [<ide options>] <out_dir>

  Generates ninja files from the current tree and puts them in the given output
  directory.

  The output directory can be a source-repo-absolute path name such as:
      //out/foo
  Or it can be a directory relative to the current directory such as:
      out/foo

  "gn gen --check" is the same as running "gn check". "gn gen --check=system" is
  the same as running "gn check --check-system".  See "gn help check" for
  documentation on that mode.

  See "gn help switches" for the common command-line switches.

General options

  --ninja-executable=<string>
      Can be used to specify the ninja executable to use. This executable will
      be used as an IDE option to indicate which ninja to use for building. This
      executable will also be used as part of the gen process for triggering a
      restat on generated ninja files and for use with --clean-stale.

  --clean-stale
      This option will cause no longer needed output files to be removed from
      the build directory, and their records pruned from the ninja build log and
      dependency database after the ninja build graph has been generated. This
      option requires a ninja executable of at least version 1.10.0. It can be
      provided by the --ninja-executable switch. Also see "gn help clean_stale".

IDE options

  GN optionally generates files for IDE. Files won't be overwritten if their
  contents don't change. Possibilities for <ide options>

  --ide=<ide_name>
      Generate files for an IDE. Currently supported values:
      "eclipse" - Eclipse CDT settings file.
      "vs" - Visual Studio project/solution files.
             (default Visual Studio version: 2022)
      "vs2013" - Visual Studio 2013 project/solution files.
      "vs2015" - Visual Studio 2015 project/solution files.
      "vs2017" - Visual Studio 2017 project/solution files.
      "vs2019" - Visual Studio 2019 project/solution files.
      "vs2022" - Visual Studio 2022 project/solution files.
      "xcode" - Xcode workspace/solution files.
      "qtcreator" - QtCreator project files.
      "json" - JSON file containing target information

  --filters=<path_prefixes>
      Semicolon-separated list of label patterns used to limit the set of
      generated projects (see "gn help label_pattern"). Only matching targets
      and their dependencies will be included in the solution. Only used for
      Visual Studio, Xcode and JSON.

Visual Studio Flags

  --sln=<file_name>
      Override default sln file name ("all"). Solution file is written to the
      root build directory.

  --no-deps
      Don't include targets dependencies to the solution. Changes the way how
      --filters option works. Only directly matching targets are included.

  --winsdk=<sdk_version>
      Use the specified Windows 10 SDK version to generate project files.
      As an example, "10.0.15063.0" can be specified to use Creators Update SDK
      instead of the default one.

  --ninja-executable=<string>
      Can be used to specify the ninja executable to use when building.

  --ninja-extra-args=<string>
      This string is passed without any quoting to the ninja invocation
      command-line. Can be used to configure ninja flags, like "-j".

Xcode Flags

  --xcode-project=<file_name>
      Override default Xcode project file name ("all"). The project file is
      written to the root build directory.

  --xcode-build-system=<value>
      Configure the build system to use for the Xcode project. Supported
      values are (default to "legacy"):
      "legacy" - Legacy Build system
      "new" - New Build System

  --xcode-configs=<config_name_list>
      Configure the list of build configuration supported by the generated
      project. If specified, must be a list of semicolon-separated strings.
      If omitted, a single configuration will be used in the generated
      project derived from the build directory.

  --xcode-config-build-dir=<string>
      If present, must be a path relative to the source directory. It will
      default to $root_out_dir if omitted. The path is assumed to point to
      the directory where ninja needs to be invoked. This variable can be
      used to build for multiple configuration / platform / environment from
      the same generated Xcode project (assuming that the user has created a
      gn build directory with the correct args.gn for each).

      One useful value is to use Xcode variables such as '${CONFIGURATION}'
      or '${EFFECTIVE_PLATFORM}'.

  --xcode-additional-files-patterns=<pattern_list>
      If present, must be a list of semicolon-separated file patterns. It
      will be used to add all files matching the pattern located in the
      source tree to the project. It can be used to add, e.g. documentation
      files to the project to allow easily edit them.

  --xcode-additional-files-roots=<path_list>
      If present, must be a list of semicolon-separated paths. It will be used
      as roots when looking for additional files to add. If omitted, defaults
      to "//".

  --ninja-executable=<string>
      Can be used to specify the ninja executable to use when building.

  --ninja-extra-args=<string>
      This string is passed without any quoting to the ninja invocation
      command-line. Can be used to configure ninja flags, like "-j".

  --ide-root-target=<target_name>
      Name of the target corresponding to "All" target in Xcode. If unset,
      "All" invokes ninja without any target and builds everything.

QtCreator Flags

  --ide-root-target=<target_name>
      Name of the root target for which the QtCreator project will be generated
      to contain files of it and its dependencies. If unset, the whole build
      graph will be emitted.


Eclipse IDE Support

  GN DOES NOT generate Eclipse CDT projects. Instead, it generates a settings
  file which can be imported into an Eclipse CDT project. The XML file contains
  a list of include paths and defines. Because GN does not generate a full
  .cproject definition, it is not possible to properly define includes/defines
  for each file individually. Instead, one set of includes/defines is generated
  for the entire project. This works fairly well but may still result in a few
  indexer issues here and there.

Generic JSON Output

  Dumps target information to a JSON file and optionally invokes a
  python script on the generated file. See the comments at the beginning
  of json_project_writer.cc and desc_builder.cc for an overview of the JSON
  file format.

  --json-file-name=<json_file_name>
      Overrides default file name (project.json) of generated JSON file.

  --json-ide-script=<path_to_python_script>
      Executes python script after the JSON file is generated or updated with
      new content. Path can be project absolute (//), system absolute (/) or
      relative, in which case the output directory will be base. Path to
      generated JSON file will be first argument when invoking script.

  --json-ide-script-args=<argument>
      Optional second argument that will be passed to executed script.

  --filter-with-data
      Additionally follows data deps when filtering. Without this flag, only
      public and private linked deps will be followed. Only used with --filters.

Ninja Outputs

  The --ninja-outputs-file=<FILE> option dumps a JSON file that maps GN labels
  to their Ninja output paths. This can be later processed to build an index
  to convert between Ninja targets and GN ones before or after the build itself.
  It looks like:

    {
      "label1": [
        "path1",
        "path2"
      ],
      "label2": [
        "path3"
      ]
    }

  --ninja-outputs-script=<path_to_python_script>
    Executes python script after the outputs file is generated or updated
    with new content. Path can be project absolute (//), system absolute (/) or
    relative, in which case the output directory will be base. Path to
    generated file will be first argument when invoking script.

  --ninja-outputs-script-args=<argument>
    Optional second argument that will be passed to executed script.

Compilation Database

  --export-rust-project
      Produces a rust-project.json file in the root of the build directory
      This is used for various tools in the Rust ecosystem allowing for the
      replay of individual compilations independent of the build system.
      This is an unstable format and likely to change without warning.

  --add-export-compile-commands=<label_pattern>
      Adds an additional label pattern (see "gn help label_pattern") of a
      target to add to the compilation database. This pattern is appended to any
      list values specified in the export_compile_commands variable in the
      .gn file (see "gn help dotfile"). This allows the user to add additional
      targets to the compilation database that the project doesn't add by default.

      To add more than one value, specify this switch more than once. Each
      invocation adds an additional label pattern.

      Example:
        --add-export-compile-commands=//tools:my_tool
        --add-export-compile-commands="//base/*"

  --export-compile-commands[=<target_name1,target_name2...>]
      DEPRECATED https://bugs.chromium.org/p/gn/issues/detail?id=302.
      Please use --add-export-compile-commands for per-user configuration, and
      the "export_compile_commands" value in the project-level .gn file (see
      "gn help dotfile") for per-project configuration.

      Overrides the value of the export_compile_commands in the .gn file (see
      "gn help dotfile") as well as the --add-export-compile-commands switch.

      Unlike the .gn setting, this switch takes a legacy format which is a list
      of target names that are matched in any directory. For example, "foo" will
      match:
       - "//path/to/src:foo"
       - "//other/path:foo"
       - "//foo:foo"
      and not match:
       - "//foo:bar"
)";

int RunGen(const std::vector<std::string>& args) {
  base::ElapsedTimer timer;

  if (args.size() != 1) {
    Err(Location(), "Need exactly one build directory to generate.",
        "I expected something more like \"gn gen out/foo\"\n"
        "You can also see \"gn help gen\".")
        .PrintToStdout();
    return 1;
  }

  // Deliberately leaked to avoid expensive process teardown.
  Setup* setup = new Setup();
  // Generate an empty args.gn file if it does not exists
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kArgs)) {
    setup->set_gen_empty_args(true);
  }
  if (!setup->DoSetup(args[0], true))
    return 1;

  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kSwitchCheck)) {
    setup->set_check_public_headers(true);
    if (command_line->GetSwitchValueString(kSwitchCheck) == "system")
      setup->set_check_system_includes(true);
  }

  // If this is a regeneration, replace existing build.ninja and build.ninja.d
  // with just enough for ninja to call GN and regenerate ninja files. This
  // removes any potential soon-to-be-dangling references and ensures that
  // regeneration can be restarted if interrupted.
  if (command_line->HasSwitch(switches::kRegeneration)) {
    if (!commands::PrepareForRegeneration(&setup->build_settings())) {
      return 1;
    }
  }

  // Cause the load to also generate the ninja files for each target.
  TargetWriteInfo write_info;
  write_info.want_ninja_outputs =
      command_line->HasSwitch(kSwitchNinjaOutputsFile);

  setup->builder().set_resolved_and_generated_callback(
      [&write_info](const BuilderRecord* record) {
        ItemResolvedAndGeneratedCallback(&write_info, record);
      });

  // Do the actual load. This will also write out the target ninja files.
  if (!setup->Run())
    return 1;

  if (command_line->HasSwitch(switches::kVerbose))
    OutputString("Build graph constructed in " +
                 base::Int64ToString(timer.Elapsed().InMilliseconds()) +
                 "ms\n");

  // Sort the targets in each toolchain according to their label. This makes
  // the ninja files have deterministic content.
  for (auto& cur_toolchain : write_info.rules) {
    std::sort(cur_toolchain.second.begin(), cur_toolchain.second.end(),
              [](const NinjaWriter::TargetRulePair& a,
                 const NinjaWriter::TargetRulePair& b) {
                return a.first->label() < b.first->label();
              });
  }

  Err err;
  // Write the root ninja files.
  if (!NinjaWriter::RunAndWriteFiles(&setup->build_settings(), setup->builder(),
                                     write_info.rules, &err)) {
    err.PrintToStdout();
    return 1;
  }

  if (!RunNinjaPostProcessTools(
          &setup->build_settings(),
          command_line->GetSwitchValuePath(switches::kNinjaExecutable),
          command_line->HasSwitch(switches::kRegeneration),
          command_line->HasSwitch(kSwitchCleanStale), &err)) {
    err.PrintToStdout();
    return 1;
  }

  if (write_info.want_ninja_outputs) {
    ElapsedTimer outputs_timer;
    std::string file_name =
        command_line->GetSwitchValueString(kSwitchNinjaOutputsFile);
    if (file_name.empty()) {
      Err(Location(), "The --ninja-outputs-file argument cannot be empty!")
          .PrintToStdout();
      return 1;
    }

    bool quiet = command_line->HasSwitch(switches::kQuiet);

    std::string exec_script =
        command_line->GetSwitchValueString(kSwitchNinjaOutputsScript);

    std::string exec_script_extra_args =
        command_line->GetSwitchValueString(kSwitchNinjaOutputsScriptArgs);

    bool res = NinjaOutputsWriter::RunAndWriteFiles(
        write_info.ninja_outputs_map, &setup->build_settings(), file_name,
        exec_script, exec_script_extra_args, quiet, &err);
    if (!res) {
      err.PrintToStdout();
      return 1;
    }
    if (!command_line->HasSwitch(switches::kQuiet)) {
      OutputString(base::StringPrintf(
          "Generating Ninja outputs file took %" PRId64 "ms\n",
          outputs_timer.Elapsed().InMilliseconds()));
    }
  }

  if (!WriteRuntimeDepsFilesIfNecessary(&setup->build_settings(),
                                        setup->builder(), &err)) {
    err.PrintToStdout();
    return 1;
  }

  if (!CheckForInvalidGeneratedInputs(setup))
    return 1;

  for (auto&& ide : command_line->GetSwitchValueStrings(kSwitchIde)) {
    if (!RunIdeWriter(ide, &setup->build_settings(), setup->builder(), &err)) {
      err.PrintToStdout();
      return 1;
    }
  }

  if (!RunCompileCommandsWriter(*setup, &err)) {
    err.PrintToStdout();
    return 1;
  }

  if (command_line->HasSwitch(kSwitchExportRustProject) &&
      !RunRustProjectWriter(&setup->build_settings(), setup->builder(), &err)) {
    err.PrintToStdout();
    return 1;
  }

  if (!WriteIgnoreFile(*setup, &err)) {
    err.PrintToStdout();
    return 1;
  }

  TickDelta elapsed_time = timer.Elapsed();

  if (!command_line->HasSwitch(switches::kQuiet)) {
    OutputString("Done. ", DECORATION_GREEN);

    size_t targets_collected = 0;
    for (const auto& rules : write_info.rules)
      targets_collected += rules.second.size();

    std::string stats =
        "Made " + base::NumberToString(targets_collected) + " targets from " +
        base::IntToString(
            setup->scheduler().input_file_manager()->GetInputFileCount()) +
        " files in " + base::Int64ToString(elapsed_time.InMilliseconds()) +
        "ms\n";
    OutputString(stats);
  }

  // Just like the build graph, leak the resolved data to avoid expensive
  // process teardown here too.
#ifndef ASAN_ENABLED
  write_info.LeakOnPurpose();
#endif

  return 0;
}

}  // namespace commands
