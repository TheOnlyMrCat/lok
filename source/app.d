module app;

import parsing;

import std.getopt;
import std.stdio;
import std.string;
import std.experimental.logger;

debug extern(C) extern __gshared int yy_flex_debug, yydebug;

void main(string[] args) {
    bool verbose;
    bool vverbose;
    bool flexDebug;
    bool bisonDebug;

    GetoptResult opts = getopt(args,
    "v|verbose", "Print information messages to standard out.", &verbose,
    "loquacious", "Print debug messages to standard out.", &vverbose,
    "garrulous", "Print scanner debug output. Indended for developers of the language. Requires debug version.", &flexDebug,
    "palaverous", "Print parser debug output as well. Don't do this. Requires debug version.", &bisonDebug);

    debug {
        yy_flex_debug = flexDebug || bisonDebug ? 1 : 0;
        yydebug = bisonDebug ? 1 : 0;
    }

    LogLevel loggerL;
    vverbose = vverbose || flexDebug || bisonDebug;
    if (vverbose) {
        loggerL = LogLevel.all;
    } else if (verbose) {
        loggerL = LogLevel.info;
    } else {
        loggerL = LogLevel.warning;
    }

    sharedLog = new class Logger {
        this() {
            super(loggerL);
        }

        override void writeLogMsg(ref LogEntry payload) {
            string formatString;
            switch (payload.logLevel) {
                case LogLevel.trace:
                    formatString = "36";
                    break;
                case LogLevel.info:
                    formatString = "32";
                    break;
                case LogLevel.warning:
                    formatString = "33";
                    break;
                case LogLevel.error:
                    formatString = "91";
                    break;
                case LogLevel.critical:
                    formatString = "31";
                    break;
                case LogLevel.fatal:
                    formatString = "35;1";
                    break;
                default: break;
            }
            writefln("[\x1b[%sm%s\x1b[0m] %s", formatString, payload.logLevel, payload.msg);
        }
    };

    if (opts.helpWanted) {
        defaultGetoptPrinter("clok: Compiler for the Lok programming language", opts.options);
        return;
    }

    if (args.length > 1) {
        foreach (filename; args[1..$]) {
            infof("Parsing file '%s'", filename);
            parse(filename);
        }
    }

    cleanupStrings();
}

void printAST(ASTNode node, int depth) {
    for (int i = 0; i < depth; i++) {
        write("-");
    }

    writef("(%s)", node.type.mapEnum);

    if (node.isStringType) {
        writeln(" \"", node.valC, "\"");
    } else if (node.isIntType) {
        writeln(" ", node.valI);
    } else if (node.isFloatType) {
        writeln(" ", node.valF);
    } else {
        writeln();
    }

    depth++;
    for (int i = 0; i < node.children.length; i++) {
        printAST(node.children[i], depth);
    }
}
