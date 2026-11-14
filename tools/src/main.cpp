// SPDX-License-Identifier: GPL-3.0-or-later
// lvdsim-cli: one binary that behaves as vlib, vmap, vcom, vlog or vsim depending on the name it is
// started with (copies or links named like the tool), or as `lvdsim-cli <tool> ...`.
// The environment variables it understands are listed in docs/command-compat.md.
#include "environment.hpp"
#include "library_map.hpp"
#include "process.hpp"
#include "project_cli.hpp"
#include "translate.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace lvd::tools;

namespace {

std::string toolNameFromProgram(const std::string& argv0) {
    std::string stem = fs::path(argv0).stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return stem;
}

void printWarnings(const Plan& plan) {
    for (const std::string& w : plan.warnings)
        std::cerr << "** Warning: " << w << "\n";
}

int execute(const Plan& plan, const std::string& tool, const Environment& env) {
    printWarnings(plan);
    if (!plan.ok()) {
        std::cerr << "** Error: (" << tool << ") " << plan.error << "\n";
        return 1;
    }

    if (plan.gui) {
        std::vector<std::string> argv{env.guiExe};
        argv.insert(argv.end(), plan.guiArgs.begin(), plan.guiArgs.end());
        const ProcessResult r = runProcess(argv, env.processOptions());
        if (!r.started) {
            std::cerr << "** Error: (" << tool << ") " << r.error
                      << "; set LVDSIM_GUI to the lvdsim-gui executable\n";
            return 1;
        }
        return r.exitCode;
    }

    for (const std::string& d : plan.directories) {
        std::error_code ec;
        fs::create_directories(d, ec);
        if (ec) {
            std::cerr << "** Error: (" << tool << ") cannot create directory '" << d << "': " << ec.message()
                      << "\n";
            return 1;
        }
    }

    for (const auto& [name, path] : plan.mappings) {
        const std::string text = readTextFile(env.iniPath).value_or("");
        std::ofstream out(env.iniPath, std::ios::binary | std::ios::trunc);
        out << withLibraryMapping(text, name, path);
    }

    if (!plan.message.empty())
        std::cout << plan.message << std::flush;

    for (const TempFile& f : plan.keepFiles) {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << f.contents;
    }

    std::vector<std::string> tempPaths;
    for (const TempFile& f : plan.files) {
        std::error_code ec;
        fs::create_directories(fs::path(f.path).parent_path(), ec);
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << f.contents;
        tempPaths.push_back(f.path);
    }

    int rc = 0;
    for (const NvcCall& call : plan.calls) {
        std::vector<std::string> argv{env.nvc};
        argv.insert(argv.end(), call.args.begin(), call.args.end());
        const ProcessResult r = runProcess(argv, env.processOptions());
        if (!r.started) {
            std::cerr << "** Error: (" << tool << ") " << r.error
                      << "; set LVDSIM_NVC to the nvc executable\n";
            rc = 1;
            break;
        }
        if (r.exitCode != 0) {
            rc = r.exitCode;
            break;
        }
    }

    for (const std::string& t : tempPaths) {
        std::error_code ec;
        fs::remove(t, ec);
        fs::remove(fs::path(t).parent_path(), ec); // only succeeds if empty
    }
    return rc;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string tool = toolNameFromProgram(argc > 0 ? argv[0] : "");
    if (tool == "lvdsim-cli" || tool == "lvdsim") {
        if (args.empty()) {
            std::cerr << "usage: lvdsim-cli <vlib|vmap|vcom|vlog|vsim|compile|regress|project> [options]\n";
            return 2;
        }
        tool = args.front();
        args.erase(args.begin());
    }

    const Environment env = discoverEnvironment();
    // Project commands (docs/projects.md): they run several nvc calls themselves instead of producing a plan.
    using ProjectCommand = int (*)(const std::vector<std::string>&, const Environment&);
    static const std::pair<const char*, ProjectCommand> kProjectCommands[] = {
        {"compile", runCompileCommand}, {"regress", runRegressCommand}, {"project", runProjectCommand}};
    for (const auto& [name, run] : kProjectCommands)
        if (tool == name)
            return run(args, env);

    Plan plan;
    if (tool == "vlib")
        plan = planVlib(args, env.ctx);
    else if (tool == "vmap")
        plan = planVmap(args, env.ctx);
    else if (tool == "vcom")
        plan = planVcom(args, env.ctx);
    else if (tool == "vlog")
        plan = planVlog(args, env.ctx);
    else if (tool == "vsim")
        plan = planVsim(args, env.ctx);
    else {
        std::cerr << "lvdsim-cli: unknown tool '" << tool
                  << "' (expected vlib, vmap, vcom, vlog, vsim, compile, regress or project)\n";
        return 2;
    }
    return execute(plan, tool, env);
}
