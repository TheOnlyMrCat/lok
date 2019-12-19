#pragma once

#include <string>
#include <memory>

#include <plog/Log.h>

#include "grammar.hpp"

extern std::unique_ptr<Node> parseResult;
extern std::string filename;
extern std::vector<std::string> strings;

strings_t getString(std::string string);

int parse();
void dumpAST(std::string file);