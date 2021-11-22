#include "lalrbld.h"
#include "parser.h"

#include <algorithm>
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
        "};",
    };
    // clang-format on
    outp << std::endl;
    for (const auto& l : text) { outp << l << std::endl; }
}

void outputParserEngine(std::ostream& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "std::pair<bool, int> parse(CtxData& ctx, std::vector<int>& state_stack, int tt) {",
        "    enum { kShiftFlag = 1, kFlagCount = 1 };",
        "    int action = -1;",
        "    if (!ctx.rise_error) {",
        "        const int* action_tbl = &action_list[action_idx[state_stack.back()]];",
        "        while (action_tbl[0] >= 0 && action_tbl[0] != tt) { action_tbl += 2; }",
        "        action = action_tbl[1];",
        "    }",
        "    ctx.reduce_length = 0;",
        "    ctx.rise_error = false;",
        "    if (action < 0) {  // Roll back to the state accepting error",
        "        ctx.can_recover = false;",
        "        do {",
        "            const int* action_tbl = &action_list[action_idx[state_stack.back()]];",
        "            while (action_tbl[0] >= 0 && action_tbl[0] != predef_tt_error) { action_tbl += 2; }",
        "            int err_action = action_tbl[1];",
        "            if (err_action >= 0 && (err_action & kShiftFlag)) {  // Shift error token",
        "                state_stack.push_back(err_action >> kFlagCount);",
        "                ctx.can_recover = true;  // Can recover",
        "                break;",
        "            }",
        "            ++ctx.reduce_length;",
        "            state_stack.pop_back();",
        "        } while (!state_stack.empty());",
        "        return {true, action};",
        "    } else if (action & kShiftFlag) {",
        "        state_stack.push_back(action >> kFlagCount);",
        "        return {false, 1};",
        "    }",
        "    const int* info = &reduce_info[action >> kFlagCount];",
        "    ctx.reduce_length = info[0];",
        "    state_stack.erase(state_stack.end() - info[0], state_stack.end());",
        "    int state = state_stack.back();",
        "    const int* goto_tbl = &goto_list[info[1]];",
        "    while (goto_tbl[0] >= 0 && goto_tbl[0] != state) { goto_tbl += 2; }",
        "    state_stack.push_back(goto_tbl[1]);",
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
                logger::fatal() << "unknown flag \'" << arg << "\'";
                return -1;
            }
        }

        if (input_file_name.empty()) {
            logger::fatal() << "no input file specified";
            return -1;
        }

        std::ifstream ifile(input_file_name);
        if (!ifile) {
            logger::fatal() << "can\'t open input file \'" << input_file_name << "\'";
            return -1;
        }

        Grammar grammar;
        Parser parser(ifile, input_file_name, grammar);
        if (!parser.parse()) { return -1; }

        LRBuilder lr_builder(grammar);
        lr_builder.buildAnalizer();

        if (unsigned sr_count = lr_builder.getSRConflictCount(); sr_count > 0) {
            logger::warning(input_file_name) << sr_count << " shift/reduce conflict(s) found";
        }
        if (unsigned rr_count = lr_builder.getRRConflictCount(); rr_count > 0) {
            logger::warning(input_file_name) << rr_count << " shift/reduce conflict(s) found";
        }

        if (!report_file_name.empty()) {
            if (std::ofstream ofile(report_file_name); ofile) {
                grammar.printTokens(ofile);
                grammar.printNonterms(ofile);
                grammar.printActions(ofile);
                grammar.printGrammar(ofile);
                lr_builder.printFirstTable(ofile);
                lr_builder.printAetaTable(ofile);
                lr_builder.printStates(ofile);
            } else {
                logger::error() << "can\'t open report file \'" << report_file_name << "\'";
            }
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
            logger::error() << "can\'t open output file \'" << defs_file_name << "\'";
        }

        const auto& action_table = lr_builder.getCompressedActionTable();
        std::vector<int> action_idx(action_table.index.size()), action_list;
        action_list.reserve(2 * action_table.data.size());
        auto action_code = [](const LRBuilder::Action& action) {
            enum { kShiftFlag = 1, kFlagCount = 1 };
            switch (action.type) {
                case LRBuilder::Action::Type::kShift: return static_cast<int>(action.val << kFlagCount) | kShiftFlag;
                case LRBuilder::Action::Type::kReduce: return static_cast<int>(3 * action.val) << kFlagCount;
                default: break;
            }
            return -1;
        };
        std::transform(action_table.index.begin(), action_table.index.end(), action_idx.begin(),
                       [](unsigned i) { return 2 * i; });
        for (const auto& [n_state, action] : action_table.data) {
            action_list.push_back(n_state);
            action_list.push_back(action_code(action));
        }

        const auto& goto_table = lr_builder.getCompressedGotoTable();
        std::vector<int> goto_list;
        goto_list.reserve(2 * goto_table.data.size());
        for (const auto& [n_nonterm, n_new_state] : goto_table.data) {
            goto_list.push_back(n_nonterm);
            goto_list.push_back(n_new_state);
        }

        if (std::ofstream ofile(analyzer_file_name); ofile) {
            ofile << "// Parsegen autogenerated analyzer file - do not edit!" << std::endl;
            outputArray(ofile, "action_idx", action_idx.begin(), action_idx.end());
            outputArray(ofile, "action_list", action_list.begin(), action_list.end());

            std::vector<int> reduce_info;
            for (unsigned n_prod = 0; n_prod < grammar.getProductionCount(); ++n_prod) {
                const auto& prod = grammar.getProductionInfo(n_prod);
                reduce_info.push_back(static_cast<int>(prod.right.size()));        // Length
                reduce_info.push_back(2 * goto_table.index[getIndex(prod.left)]);  // Goto index
                reduce_info.push_back(prod.action);                                // Action on reduce
            }

            outputArray(ofile, "reduce_info", reduce_info.begin(), reduce_info.end());
            outputArray(ofile, "goto_list", goto_list.begin(), goto_list.end());
            outputParserEngine(ofile);
        } else {
            logger::error() << "can\'t open output file \'" << analyzer_file_name << "\'";
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal() << "exception catched: " << e.what(); }
    return -1;
}
