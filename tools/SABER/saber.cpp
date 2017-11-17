//===- saber.cpp -- Source-sink bug checker------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2016>  <Yulei Sui>
// Copyright (C) <2013-2016>  <Jingling Xue>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===-----------------------------------------------------------------------===//

/*
 // Saber: Software Bug Check.
 //
 // Author: Yulei Sui,
 */

#include "SABER/LeakChecker.h"
#include "SABER/FileChecker.h"
#include "SABER/DoubleFreeChecker.h"
#include "SABER/UseAfterFreeChecker.h"
#include "SABER/Profiler.h"

#include <llvm/Support/CommandLine.h>	// for cl
#include <llvm/Bitcode/BitcodeWriterPass.h>  // for bitcode write
#include <llvm/IR/LegacyPassManager.h>		// pass manager
#include <llvm/Support/Signals.h>	// singal for command line
#include <llvm/IRReader/IRReader.h>	// IR reader for bit file
#include <llvm/Support/ToolOutputFile.h> // for tool output file
#include <llvm/Support/PrettyStackTrace.h> // for pass list
#include <llvm/IR/LLVMContext.h>		// for llvm LLVMContext
#include <llvm/Support/SourceMgr.h> // for SMDiagnostic
#include <llvm/Bitcode/ReaderWriter.h>		// for createBitcodeWriterPass

#include <unistd.h>
#include <signal.h>

using namespace llvm;

static cl::opt<std::string> InputFilename(cl::Positional,
        cl::desc("<input bitcode>"), cl::init("-"));

static cl::opt<bool> LEAKCHECKER("leak", cl::init(false),
                                 cl::desc("Memory Leak Detection"));

static cl::opt<bool> FILECHECKER("fileck", cl::init(false),
                                 cl::desc("File Open/Close Detection"));

static cl::opt<bool> DFREECHECKER("dfree", cl::init(false),
                                  cl::desc("Double Free Detection"));

static cl::opt<bool> UAFCHECKER("uaf", cl::init(false),
                                 cl::desc("Use After Free Detection"));

Profiler *globalprofiler = nullptr;

extern unsigned Index;

void timeout(int x) {
    globalprofiler->create_snapshot();
    outs() << "\n\n";
    globalprofiler->print_snapshot_result("Total");
    globalprofiler->print_peak_memory();
    outs() << "\n Report " << Index << " bugs!\n";
    outs() << "\n[!!!] Exit because of timeout > 12 hours!\n";
    exit(0);
}

int main(int argc, char ** argv) {

    globalprofiler = new Profiler(Profiler::TIME | Profiler::MEMORY);
    globalprofiler->reset();

    signal(SIGALRM, timeout);
    alarm(12*3600);

    sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);

    LLVMContext &Context = getGlobalContext();

    std::string OutputFilename;

    cl::ParseCommandLineOptions(argc, argv, "Software Bug Check\n");
    sys::PrintStackTraceOnErrorSignal();

    PassRegistry &Registry = *PassRegistry::getPassRegistry();

    initializeCore(Registry);
    initializeScalarOpts(Registry);
    initializeIPO(Registry);
    initializeAnalysis(Registry);
    initializeTransformUtils(Registry);
    initializeInstCombine(Registry);
    initializeInstrumentation(Registry);
    initializeTarget(Registry);

    llvm::legacy::PassManager Passes;

    SMDiagnostic Err;

    // Load the input module...
    std::unique_ptr<Module> M1 = parseIRFile(InputFilename, Err, Context);

    if (!M1) {
        Err.print(argv[0], errs());
        return 1;
    }

    std::unique_ptr<tool_output_file> Out;
    std::error_code ErrorInfo;

    StringRef str(InputFilename);
    InputFilename = str.rsplit('.').first;
    OutputFilename = InputFilename + ".saber";

    Out.reset(
        new tool_output_file(OutputFilename.c_str(), ErrorInfo,
                             sys::fs::F_None));

    if (ErrorInfo) {
        errs() << ErrorInfo.message() << '\n';
        return 1;
    }

    if(LEAKCHECKER)
        Passes.add(new LeakChecker());
    else if(FILECHECKER)
        Passes.add(new FileChecker());
    else if(DFREECHECKER)
        Passes.add(new DoubleFreeChecker());
    else if(UAFCHECKER)
        Passes.add(new UseAfterFreeChecker());

    Passes.add(createBitcodeWriterPass(Out->os()));

    Passes.run(*M1.get());
    Out->keep();

    outs() << "\n Report " << Index << " bugs!\n";
    globalprofiler->create_snapshot();
    outs() << "\n\n";
    globalprofiler->print_snapshot_result("Total");
    globalprofiler->print_peak_memory();

    return 0;

}

