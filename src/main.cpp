#include "lalrbld.h"
#include "parser.h"

#include <fstream>

template<typename Iter>
void outputData(std::ostream& ofile, Iter from, Iter to, size_t ntab = 0) {
    auto convert_to_string = [](const auto& v) {
        if constexpr (std::is_constructible<std::string, decltype(v)>::value) {
            return '\"' + v + '\"';
        } else {
            return std::to_string(v);
        }
    };

    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + convert_to_string(*from);
    while (++from != to) {
        auto sval = convert_to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            ofile << line << "," << std::endl;
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    ofile << line << std::endl;
}

template<typename Iter>
void outputArray(std::ostream& ofile, std::string_view array_name, Iter from, Iter to) {
    if (from == to) { return; }
    if constexpr (std::is_constructible<std::string_view, decltype(*from)>::value) {
        ofile << std::endl << "static const char* ";
    } else {
        ofile << std::endl << "static int ";
    }
    ofile << array_name << "[" << std::distance(from, to) << "] = {" << std::endl;
    outputData(ofile, from, to, 4);
    ofile << "};" << std::endl;
}

void outputParserDefs(std::ostream& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "struct CtxData {",
        "    unsigned reduce_length = 0;",
        "    bool can_recover = false;",
        "    bool rise_error = false;",
        "    std::vector<int> state_stack;",
        "};",
    };
    // clang-format on
    outp << std::endl;
    for (const auto& l : text) { outp << l << std::endl; }
}

void outputParserEngine(std::ostream& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "std::pair<bool, int> parse(CtxData& ctx, int tt) {",
        "    enum { kShiftFlag = 1, kFlagCount = 1 };",
        "    int action = -1;",
        "    if (!ctx.rise_error) {",
        "        const int* action_tbl = &action_list[action_idx[ctx.state_stack.back()]];",
        "        while (action_tbl[0] >= 0 && action_tbl[0] != tt) { action_tbl += 2; }",
        "        action = action_tbl[1];",
        "    }",
        "    ctx.reduce_length = 0;",
        "    ctx.rise_error = false;",
        "    if (action < 0) {  // Roll back to the state accepting error",
        "        ctx.can_recover = false;",
        "        do {",
        "            const int* action_tbl = &action_list[action_idx[ctx.state_stack.back()]];",
        "            while (action_tbl[0] >= 0 && action_tbl[0] != predef_tt_error) { action_tbl += 2; }",
        "            int err_action = action_tbl[1];",
        "            if (err_action >= 0 && (err_action & kShiftFlag)) {  // Shift error token",
        "                ctx.state_stack.push_back(err_action >> kFlagCount);",
        "                ctx.can_recover = true;  // Can recover",
        "                break;",
        "            }",
        "            ++ctx.reduce_length;",
        "            ctx.state_stack.pop_back();",
        "        } while (!ctx.state_stack.empty());",
        "        return {true, action};",
        "    } else if (action & kShiftFlag) {",
        "        ctx.state_stack.push_back(action >> kFlagCount);",
        "        return {false, 1};",
        "    }",
        "    const int* info = &reduce_info[action >> kFlagCount];",
        "    ctx.reduce_length = info[0];",
        "    ctx.state_stack.resize(ctx.state_stack.size() - info[0]);",
        "    int state = ctx.state_stack.back();",
        "    const int* goto_tbl = &goto_list[info[1]];",
        "    while (goto_tbl[0] >= 0 && goto_tbl[0] != state) { goto_tbl += 2; }",
        "    ctx.state_stack.push_back(goto_tbl[1]);",
        "    return {true, info[2]};",
        "}",
    };
    // clang-format on
    outp << std::endl;
    for (const auto& l : text) { outp << l << std::endl; }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        std::string input_file_name;
        std::string analyzer_file_name("parser_analyzer.inl");
        std::string defs_file_name("parser_defs.h");
        std::string report_file_name;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "-o") {
                if (++i < argc) { analyzer_file_name = argv[i]; }
            } else if (arg == "-h") {
                if (++i < argc) { defs_file_name = argv[i]; }
            } else if (arg == "--report") {
                if (++i < argc) { report_file_name = argv[i]; }
            } else if (arg == "--help") {
                // clang-format off
                static const char* text[] = {
                    "Usage: parsegen [options] file",
                    "Options:",
                    "    -o <file>           Place the output analyzer into <file>.",
                    "    -h <file>           Place the output definitions into <file>.",
                    "    --report <file>     Place analyzer build report into <file>.",
                    "    --help              Display this information.",
                };
                // clang-format on
                for (const char* l : text) { std::cout << l << std::endl; }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = argv[i];
            } else {
                std::cerr << "parsegen: unknown flag `" << arg << "`." << std::endl;
                return -1;
            }
        }

        if (input_file_name.empty()) {
            std::cerr << "parsegen: no input file specified." << std::endl;
            return -1;
        }

        std::ifstream ifile(input_file_name);
        if (!ifile) {
            std::cerr << "parsegen: cannot open input file `" << input_file_name << "`." << std::endl;
            return -1;
        }

        Grammar grammar;
        Parser parser(ifile, input_file_name, grammar);
        int res = parser.parse();
        if (res != 0) { return res; }

        LRBuilder lr_builder(grammar);
        lr_builder.buildAnalizer();

        if (unsigned sr_count = lr_builder.getSRConflictCount(); sr_count > 0) {
            std::cerr << input_file_name << ": warning: " << sr_count << " shift/reduce conflict(s) found."
                      << std::endl;
        }
        if (unsigned rr_count = lr_builder.getRRConflictCount(); rr_count > 0) {
            std::cerr << input_file_name << ": warning: " << rr_count << " shift/reduce conflict(s) found."
                      << std::endl;
        }

        if (std::ofstream ofile(defs_file_name); ofile) {
            ofile << "// Parsegen autogenerated definition file - do not edit!" << std::endl;
            ofile << std::endl << "enum {" << std::endl;
            ofile << "    predef_tt_end = " << kTokenEnd << "," << std::endl;
            ofile << "    predef_tt_error = " << kTokenError << "," << std::endl;
            unsigned last_id = kTokenError;
            for (const auto& [name, id] : grammar.getTokenList()) {
                ofile << "    tt_" << name;
                if (id > last_id + 1) { ofile << " = " << id; }
                ofile << "," << std::endl;
                last_id = id;
            }
            ofile << "};" << std::endl;

            if (const auto& action_list = grammar.getActionList(); !action_list.empty()) {
                ofile << std::endl << "enum {" << std::endl;
                unsigned last_id = action_list.front().second;
                for (const auto& [name, id] : action_list) {
                    ofile << "    act_" << name;
                    if (id != last_id + 1) { ofile << " = " << id; }
                    ofile << "," << std::endl;
                    last_id = id;
                }
                ofile << "};" << std::endl;
            }

            outputParserDefs(ofile);
        } else {
            std::cerr << "parsegen: cannot open output file `" << defs_file_name << "`." << std::endl;
        }

        std::vector<int> action_idx, action_list, goto_idx, goto_list;
        lr_builder.compressTables(action_idx, action_list, goto_idx, goto_list);

        if (std::ofstream ofile(analyzer_file_name); ofile) {
            ofile << "// Parsegen autogenerated analyzer file - do not edit!" << std::endl;
            outputArray(ofile, "action_idx", action_idx.begin(), action_idx.end());
            outputArray(ofile, "action_list", action_list.begin(), action_list.end());

            std::vector<int> reduce_info;
            for (unsigned n_prod = 0; n_prod < grammar.getProductionCount(); ++n_prod) {
                const auto& prod = grammar.getProductionInfo(n_prod);
                reduce_info.push_back(static_cast<int>(prod.right.size()));  // Length
                reduce_info.push_back(goto_idx[getIndex(prod.left)]);        // Goto index
                reduce_info.push_back(prod.action);                          // Action on reduce
            }

            outputArray(ofile, "reduce_info", reduce_info.begin(), reduce_info.end());
            outputArray(ofile, "goto_list", goto_list.begin(), goto_list.end());
            outputParserEngine(ofile);
        } else {
            std::cerr << "parsegen: cannot open output file `" << analyzer_file_name << "`." << std::endl;
        }

        if (!report_file_name.empty()) {
            if (std::ofstream ofile(report_file_name); ofile) {
                grammar.printTokens(ofile);
                grammar.printNonterms(ofile);
                grammar.printActions(ofile);
                grammar.printGrammar(ofile);
                lr_builder.printFirstTable(ofile);
                lr_builder.printAetaTable(ofile);
                lr_builder.printStates(ofile, action_idx, action_list, goto_idx, goto_list);
            } else {
                std::cerr << "parsegen: cannot open output file `" << report_file_name << "`." << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) { std::cerr << "parsegen: exception catched: " << e.what() << "." << std::endl; }
    return -1;
}
