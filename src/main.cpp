#include "lalr_builder.h"
#include "parser.h"

#include "uxs/cli/parser.h"
#include "uxs/io/filebuf.h"

#define XSTR(s) STR(s)
#define STR(s)  #s

template<typename Iter>
void outputData(uxs::iobuf& outp, Iter from, Iter to, size_t ntab = 0) {
    auto convert_to_string = [](const auto& v) {
        if constexpr (std::is_constructible<std::string, decltype(v)>::value) {
            return '\"' + v + '\"';
        } else {
            return uxs::to_string(v);
        }
    };

    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + convert_to_string(*from);
    while (++from != to) {
        auto sval = convert_to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            outp.write(line).put(',').put('\n');
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    outp.write(line).put('\n');
}

template<typename Iter>
void outputArray(uxs::iobuf& outp, std::string_view array_name, Iter from, Iter to) {
    if (from == to) { return; }
    if constexpr (std::is_constructible<std::string_view, decltype(*from)>::value) {
        outp.write("\nstatic const char* ");
    } else {
        outp.write("\nstatic int ");
    }
    uxs::fprintln(outp, "{}[{}] = {{", array_name, std::distance(from, to));
    outputData(outp, from, to, 4);
    outp.write("};\n");
}

void outputParserEngine(uxs::iobuf& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "static int parse(int tt, int* sptr0, int** p_sptr, int rise_error) {",
        "    enum { kShiftFlag = 1, kFlagCount = 1 };",
        "    int action = rise_error;",
        "    if (action >= 0) {",
        "        const int* action_tbl = &action_list[action_idx[*(*p_sptr - 1)]];",
        "        while (action_tbl[0] >= 0 && action_tbl[0] != tt) { action_tbl += 2; }",
        "        action = action_tbl[1];",
        "    }",
        "    if (action >= 0) {",
        "        if (!(action & kShiftFlag)) {",
        "            const int* info = &reduce_info[action >> kFlagCount];",
        "            const int* goto_tbl = &goto_list[info[1]];",
        "            int state = *((*p_sptr -= info[0]) - 1);",
        "            while (goto_tbl[0] >= 0 && goto_tbl[0] != state) { goto_tbl += 2; }",
        "            *(*p_sptr)++ = goto_tbl[1];",
        "            return predef_act_reduce + info[2];",
        "        }",
        "        *(*p_sptr)++ = action >> kFlagCount;",
        "        return predef_act_shift;",
        "    }",
        "    /* Roll back to state, which can accept error */",
        "    do {",
        "        const int* action_tbl = &action_list[action_idx[*(*p_sptr - 1)]];",
        "        while (action_tbl[0] >= 0 && action_tbl[0] != predef_tt_error) { action_tbl += 2; }",
        "        if (action_tbl[1] >= 0 && (action_tbl[1] & kShiftFlag)) { /* Can recover */",
        "            *(*p_sptr)++ = action_tbl[1] >> kFlagCount;           /* Shift error token */",
        "            break;",
        "        }",
        "    } while (--*p_sptr != sptr0);",
        "    return action;",
        "}",
    };
    // clang-format on
    outp.put('\n');
    for (const auto& l : text) { outp.write(l).put('\n'); }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        bool show_help = false, show_version = false;
        std::string input_file_name;
        std::string analyzer_file_name("parser_analyzer.inl");
        std::string defs_file_name("parser_defs.h");
        std::string report_file_name;
        auto cli = uxs::cli::command(argv[0])
                   << uxs::cli::overview("A tool for LALR-grammar based parser generation")
                   << uxs::cli::value("file", input_file_name)
                   << (uxs::cli::option({"-o", "--outfile="}) & uxs::cli::value("<file>", analyzer_file_name)) %
                          "Place the output analyzer into <file>."
                   << (uxs::cli::option({"--header-file="}) & uxs::cli::value("<file>", defs_file_name)) %
                          "Place the output definitions into <file>."
                   << uxs::cli::option({"-h", "--help"}).set(show_help) % "Display this information."
                   << uxs::cli::option({"-V", "--version"}).set(show_version) % "Display version.";

        auto parse_result = cli->parse(argc, argv);
        if (show_help) {
            for (auto const* node = parse_result.node; node; node = node->get_parent()) {
                if (node->get_type() == uxs::cli::node_type::kCommand) {
                    uxs::stdbuf::out.write(static_cast<const uxs::cli::basic_command<char>&>(*node).make_man_page(true));
                    break;
                }
            }
            return 0;
        } else if (show_version) {
            uxs::stdbuf::out.write(XSTR(VERSION)).endl();
            return 0;
        } else if (parse_result.status != uxs::cli::parsing_status::kOk) {
            switch (parse_result.status) {
                case uxs::cli::parsing_status::kUnknownOption: {
                    logger::fatal().format("unknown command line option `{}`", argv[parse_result.arg_count]);
                } break;
                case uxs::cli::parsing_status::kInvalidValue: {
                    if (parse_result.arg_count < argc) {
                        logger::fatal().format("invalid command line argument `{}`", argv[parse_result.arg_count]);
                    } else {
                        logger::fatal().format("expected command line argument after `{}`",
                                               argv[parse_result.arg_count - 1]);
                    }
                } break;
                case uxs::cli::parsing_status::kUnspecifiedValue: {
                    if (input_file_name.empty()) { logger::fatal().format("no input file specified"); }
                } break;
                default: break;
            }
            return -1;
        }

        uxs::filebuf ifile(input_file_name.c_str(), "r");
        if (!ifile) {
            logger::fatal().format("could not open input file `{}`", input_file_name);
            return -1;
        }

        Grammar grammar(input_file_name);
        Parser parser(ifile, input_file_name, grammar);
        if (!parser.parse()) { return -1; }

        LalrBuilder lr_builder(grammar);

        logger::info(input_file_name).format("\033[1;34mbuilding analyzer...\033[0m");
        lr_builder.build();

        logger::info(input_file_name)
            .format("{}done:\033[0m {} shift/reduce, {} reduce/reduce conflict(s) found",
                    !lr_builder.getSRConflictCount() && !lr_builder.getRRConflictCount() ? "\033[1;32m" : "\033[1;33m",
                    lr_builder.getSRConflictCount(), lr_builder.getRRConflictCount());

        if (!report_file_name.empty()) {
            if (uxs::filebuf ofile(report_file_name.c_str(), "w"); ofile) {
                grammar.printTokens(ofile);
                grammar.printNonterms(ofile);
                grammar.printActions(ofile);
                grammar.printGrammar(ofile);
                lr_builder.printFirstTable(ofile);
                lr_builder.printAetaTable(ofile);
                lr_builder.printStates(ofile);
            } else {
                logger::error().format("could not open report file `{}`", report_file_name);
            }
        }

        if (uxs::filebuf ofile(defs_file_name.c_str(), "w"); ofile) {
            ofile.write("/* Parsegen autogenerated definition file - do not edit! */\n");
            ofile.write("/* clang-format off */\n");
            ofile.write("\nenum {\n");
            uxs::fprintln(ofile, "    predef_tt_error = {},", static_cast<int>(kTokenError));
            unsigned last_tt_id = kTokenError;
            for (const auto& [name, id] : grammar.getTokenList()) {
                uxs::fprint(ofile, "    tt_{}", name);
                if (id > last_tt_id + 1) { uxs::fprint(ofile, " = {}", id); }
                ofile.put(',').put('\n');
                last_tt_id = id;
            }
            ofile.write("    total_token_count\n");
            ofile.write("};\n");

            ofile.write("\nenum {\n");
            ofile.write("    predef_act_shift = 0,\n");
            ofile.write("    predef_act_reduce = 1,\n");
            unsigned last_act_id = 0;
            for (const auto& [name, id] : grammar.getActionList()) {
                uxs::fprint(ofile, "    act_{}", name);
                if (id != last_act_id + 1) { uxs::fprint(ofile, " = {}", id + 1); }
                ofile.put(',').put('\n');
                last_act_id = id;
            }
            ofile.write("    total_action_count\n");
            ofile.write("};\n");

            if (const auto& start_conditions = grammar.getStartConditions(); !start_conditions.empty()) {
                ofile.write("\nenum {\n");
                if (start_conditions.size() > 1) {
                    uxs::fprintln(ofile, "    sc_{} = 0,", start_conditions[0].first);
                    for (size_t i = 1; i < start_conditions.size() - 1; ++i) {
                        uxs::fprintln(ofile, "    sc_{},", start_conditions[i].first);
                    }
                    uxs::fprintln(ofile, "    sc_{}", start_conditions[start_conditions.size() - 1].first);
                } else {
                    uxs::fprintln(ofile, "    sc_{} = 0", start_conditions[0].first);
                }
                ofile.write("};\n");
            }
        } else {
            logger::error().format("could not open output file `{}`", defs_file_name);
        }

        const auto& action_table = lr_builder.getCompressedActionTable();
        std::vector<int> action_idx(action_table.index.size()), action_list;
        action_list.reserve(2 * action_table.data.size());
        auto action_code = [](const LalrBuilder::Action& action) {
            enum { kShiftFlag = 1, kFlagCount = 1 };
            switch (action.type) {
                case LalrBuilder::Action::Type::kShift: return static_cast<int>(action.val << kFlagCount) | kShiftFlag;
                case LalrBuilder::Action::Type::kReduce: return static_cast<int>(3 * action.val) << kFlagCount;
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

        if (uxs::filebuf ofile(analyzer_file_name.c_str(), "w"); ofile) {
            ofile.write("/* Parsegen autogenerated analyzer file - do not edit! */\n");
            ofile.write("/* clang-format off */\n");
            outputArray(ofile, "action_idx", action_idx.begin(), action_idx.end());
            outputArray(ofile, "action_list", action_list.begin(), action_list.end());

            std::vector<int> reduce_info;
            reduce_info.reserve(3 * grammar.getProductionCount());
            for (unsigned n_prod = 0; n_prod < grammar.getProductionCount(); ++n_prod) {
                const auto& prod = grammar.getProductionInfo(n_prod);
                reduce_info.push_back(static_cast<int>(prod.rhs.size()));         // Length
                reduce_info.push_back(2 * goto_table.index[getIndex(prod.lhs)]);  // Goto index
                reduce_info.push_back(prod.action);                               // Action on reduce
            }

            outputArray(ofile, "reduce_info", reduce_info.begin(), reduce_info.end());
            outputArray(ofile, "goto_list", goto_list.begin(), goto_list.end());
            outputParserEngine(ofile);
        } else {
            logger::error().format("could not open output file `{}`", analyzer_file_name);
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal().format("exception caught: {}", e.what()); }
    return -1;
}
