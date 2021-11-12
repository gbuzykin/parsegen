%start symb
%start string

dig     [0-9]
oct_dig [0-7]
hex_dig [0-9a-fA-F]
letter  [a-zA-Z]
int     (\+?|-?){dig}+
id      ({letter}|_)({letter}|{dig}|_)*
ws      [ \t]+

%%

string_end       <string>\"
symb_end         <symb>\'
symb             <symb>[^\\\n\']
string_es_oct    <string symb>\\{oct_dig}{3}
string_es_hex    <string symb>\\x{hex_dig}{2}
string_es_a      <string symb>\\a
string_es_b      <string symb>\\b
string_es_f      <string symb>\\f
string_es_r      <string symb>\\r
string_es_n      <string symb>\\n
string_es_t      <string symb>\\t
string_es_v      <string symb>\\v
string_es_bslash <string symb>\\\\
string_es_dquot  <string symb>\\\"
string_cont      <string>[^\\\n\"]*
string_nl        <string symb>\n
string_eof       <string symb><<EOF>>

whitespace       {ws}

token            "%token"
action           "%action"
option           "%option"
left             "%left"
right            "%right"
nonassoc         "%nonassoc"
prec             "%prec"
sep              "%%"

act              \{{id}\}
int              {int}
id               {id}

string_begin     \"
symb_begin       \'
comment          #
other_char       .
nl               \n
eof              <<EOF>>

%%
