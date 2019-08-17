/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "function.h"
#include "expression.h"
#include "evalcontext.h"
#include "builtin.h"
#include "printutils.h"
#include "stackcheck.h"
#include "exceptions.h"
#include "memory.h"
#include "UserModule.h"
#include "degree_trig.h"

#include <cmath>
#include <sstream>
#include <ctime>
#include <limits>
#include <algorithm>

/*
 Random numbers

 Newer versions of boost/C++ include a non-deterministic random_device and
 auto/bind()s for random function objects, but we are supporting older systems.
*/

#include"boost-utils.h"
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real.hpp>
/*Unicode support for string lengths and array accesses*/
#include <glib.h>
// hash double
#include "linalg.h"

#if defined __WIN32__ || defined _MSC_VER
#include <process.h>
int process_id = _getpid();
#else
#include <sys/types.h>
#include <unistd.h>
int process_id = getpid();
#endif

boost::mt19937 deterministic_rng;
boost::mt19937 lessdeterministic_rng( std::time(nullptr) + process_id );

static void print_argCnt_warning(const char *name, const Context *ctx, const EvalContext *evalctx){
	PRINTB("WARNING: %s() number of parameters does not match, %s", name % evalctx->loc.toRelativeString(ctx->documentPath()));
}

static void print_argConvert_warning(const char *name, const Context *ctx, const EvalContext *evalctx){
	PRINTB("WARNING: %s() parameter could not be converted, %s", name % evalctx->loc.toRelativeString(ctx->documentPath()));
}

Value builtin_abs(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(std::fabs(v.toDouble()));
		}else{
			print_argConvert_warning("abs", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("abs", ctx, evalctx);
	}

	return Value();
}

Value builtin_sign(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER) {
			double x = v.toDouble();
			return Value((x<0) ? -1.0 : ((x>0) ? 1.0 : 0.0));
		}else{
			print_argConvert_warning("sign", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("sign", ctx, evalctx);
	}
	return Value();
}

Value builtin_rands(const Context *ctx, const EvalContext *evalctx)
{
	size_t n = evalctx->numArgs();
	if (n == 3 || n == 4) {
		Value v0 = evalctx->getArgValue(0);
		if (v0.type() != Value::ValueType::NUMBER) goto quit;
		double min = v0.toDouble();

		if (std::isinf(min) || std::isnan(min)){
			PRINTB("WARNING: rands() range min cannot be infinite, %s", evalctx->loc.toRelativeString(ctx->documentPath()));
			min = -std::numeric_limits<double>::max()/2;
			PRINTB("WARNING: resetting to %f",min);
		}
		Value v1 = evalctx->getArgValue(1);
		if (v1.type() != Value::ValueType::NUMBER) goto quit;
		double max = v1.toDouble();
		if (std::isinf(max)  || std::isnan(max)) {
			PRINTB("WARNING: rands() range max cannot be infinite, %s", evalctx->loc.toRelativeString(ctx->documentPath()));
			max = std::numeric_limits<double>::max()/2;
			PRINTB("WARNING: resetting to %f",max);
		}
		if (max < min) {
			double tmp = min; min = max; max = tmp;
		}
		Value v2 = evalctx->getArgValue(2);
		if (v2.type() != Value::ValueType::NUMBER) goto quit;
		double numresultsd = std::abs( v2.toDouble() );
		if (std::isinf(numresultsd)  || std::isnan(numresultsd)) {
			PRINTB("WARNING: rands() cannot create an infinite number of results, %s", evalctx->loc.toRelativeString(ctx->documentPath()));
			PRINT("WARNING: resetting number of results to 1");
			numresultsd = 1;
		}
		size_t numresults = boost_numeric_cast<size_t,double>( numresultsd );

		bool deterministic = false;
		if (n > 3) {
			Value v3 = evalctx->getArgValue(3);
			if (v3.type() != Value::ValueType::NUMBER) goto quit;
			uint32_t seed = static_cast<uint32_t>(hash_floating_point( v3.toDouble() ));
			deterministic_rng.seed( seed );
			deterministic = true;
		}
		Value::VectorPtr vec;
		if (min==max) { // Boost doesn't allow min == max
			for (size_t i=0; i < numresults; i++)
				vec->emplace_back(min);
		} else {
			boost::uniform_real<> distributor( min, max );
			for (size_t i=0; i < numresults; i++) {
				if ( deterministic ) {
					vec->emplace_back(distributor(deterministic_rng));
				} else {
					vec->emplace_back(distributor(lessdeterministic_rng));
				}
			}
		}
		return Value(vec);
	}else{
		print_argCnt_warning("rands", ctx, evalctx);
	}
quit:
	return Value();
}

Value builtin_min(const Context *ctx, const EvalContext *evalctx)
{
	// preserve special handling of the first argument
	// as a template for vector processing
	size_t n = evalctx->numArgs();
	if (n >= 1) {
		Value v0 = evalctx->getArgValue(0);

		if (n == 1 && v0.type() == Value::ValueType::VECTOR && !v0.toVectorPtr()->empty()) {
			const Value::VectorPtr &vec = v0.toVectorPtr();
			return std::min_element(vec->cbegin(), vec->cend())->clone();
		}
		if (v0.type() == Value::ValueType::NUMBER) {
			double val = v0.toDouble();
			for (size_t i = 1; i < n; ++i) {
				Value v = evalctx->getArgValue(i);
				// 4/20/14 semantic change per discussion:
				// break on any non-number
				if (v.type() != Value::ValueType::NUMBER) goto quit;
				double x = v.toDouble();
				if (x < val) val = x;
			}
			return Value(val);
		}
		
	}else{
		print_argCnt_warning("min", ctx, evalctx);
		return Value();
	}
quit:
	print_argConvert_warning("min", ctx, evalctx);
	return Value();
}

Value builtin_max(const Context *ctx, const EvalContext *evalctx)
{
	// preserve special handling of the first argument
	// as a template for vector processing
	size_t n = evalctx->numArgs();
	if (n >= 1) {
		Value v0 = evalctx->getArgValue(0);

		if (n == 1 && v0.type() == Value::ValueType::VECTOR && !v0.toVectorPtr()->empty()) {
			const Value::VectorPtr &vec = v0.toVectorPtr();
			return std::max_element(vec->begin(), vec->end())->clone();
		}
		if (v0.type() == Value::ValueType::NUMBER) {
			double val = v0.toDouble();
			for (size_t i = 1; i < n; ++i) {
				Value v = evalctx->getArgValue(i);
				// 4/20/14 semantic change per discussion:
				// break on any non-number
				if (v.type() != Value::ValueType::NUMBER) goto quit;
				double x = v.toDouble();
				if (x > val) val = x;
			}
			return Value(val);
		}
	}else{
		print_argCnt_warning("max", ctx, evalctx);
		return Value();
	}
quit:
	print_argConvert_warning("max", ctx, evalctx);
	return Value();
}

Value builtin_sin(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(sin_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("sin", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("sin", ctx, evalctx);
	}
	return Value();
}


Value builtin_cos(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(cos_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("cos", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("cos", ctx, evalctx);
	}
	return Value();
}

Value builtin_asin(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(asin_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("asin", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("asin", ctx, evalctx);
	}
	return Value();
}

Value builtin_acos(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(acos_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("acos", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("acos", ctx, evalctx);
	}
	return Value();
}

Value builtin_tan(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(tan_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("tan", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("tan", ctx, evalctx);
	}
	return Value();
}

Value builtin_atan(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(atan_degrees(v.toDouble()));
		}else{
			print_argConvert_warning("atan", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("atan", ctx, evalctx);
	}
	return Value();
}

Value builtin_atan2(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 2) {
		Value v0 = evalctx->getArgValue(0), v1 = evalctx->getArgValue(1);
		if (v0.type() == Value::ValueType::NUMBER && v1.type() == Value::ValueType::NUMBER){
			return Value(atan2_degrees(v0.toDouble(), v1.toDouble()));
		}else{
			print_argConvert_warning("atan2", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("atan2", ctx, evalctx);
	}
	return Value();
}

Value builtin_pow(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 2) {
		Value v0 = evalctx->getArgValue(0), v1 = evalctx->getArgValue(1);
		if (v0.type() == Value::ValueType::NUMBER && v1.type() == Value::ValueType::NUMBER){
			return Value(pow(v0.toDouble(), v1.toDouble()));
		}else{
			print_argConvert_warning("pow", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("pow", ctx, evalctx);
	}
	return Value();
}

Value builtin_round(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(round(v.toDouble()));
		}else{
			print_argConvert_warning("round", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("round", ctx, evalctx);
	}
	return Value();
}

Value builtin_ceil(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(ceil(v.toDouble()));
		}else{
			print_argConvert_warning("ceil", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("ceil", ctx, evalctx);
	}
	return Value();
}

Value builtin_floor(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(floor(v.toDouble()));
		}else{
			print_argConvert_warning("floor", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("floor", ctx, evalctx);
	}
	return Value();
}

Value builtin_sqrt(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(sqrt(v.toDouble()));
		}else{
			print_argConvert_warning("sqrt", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("sqrt", ctx, evalctx);
	}
	return Value();
}

Value builtin_exp(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(exp(v.toDouble()));
		}else{
			print_argConvert_warning("exp", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("exp", ctx, evalctx);
	}
	return Value();
}

Value builtin_length(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::VECTOR) return Value(int(v.toVectorPtr()->size()));
		if (v.type() == Value::ValueType::STRING) {
			//Unicode glyph count for the length -- rather than the string (num. of bytes) length.
			return Value(double( v.toStrUtf8Wrapper().get_utf8_strlen()));
		}
		print_argConvert_warning("len", ctx, evalctx);
	}else{
		print_argCnt_warning("len", ctx, evalctx);
	}
	return Value();
}

Value builtin_log(const Context *ctx, const EvalContext *evalctx)
{
	size_t n = evalctx->numArgs();
	if (n == 1 || n == 2) {
		Value v0 = evalctx->getArgValue(0);
		if (v0.type() == Value::ValueType::NUMBER) {
			double x = 10.0, y = v0.toDouble();
			if (n > 1) {
				Value v1 = evalctx->getArgValue(1);
				if (v1.type() != Value::ValueType::NUMBER) goto quit;
				x = y; y = v1.toDouble();
			}
			return Value(log(y) / log(x));
		}
	}else{
		print_argCnt_warning("log", ctx, evalctx);
		return Value();
	}
quit:
	print_argConvert_warning("log", ctx, evalctx);
	return Value();
}

Value builtin_ln(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(log(v.toDouble()));
		}else{
			print_argConvert_warning("ln", ctx, evalctx);
		}
	}else{
		print_argCnt_warning("ln", ctx, evalctx);
	}
	return Value();
}

Value builtin_str(const Context *, const EvalContext *evalctx)
{
	std::ostringstream stream;

	for (size_t i = 0; i < evalctx->numArgs(); i++) {
		stream << evalctx->getArgValue(i).toString();
	}
	return Value(stream.str());
}

Value builtin_chr(const Context *, const EvalContext *evalctx)
{
	std::ostringstream stream;
	
	for (size_t i = 0; i < evalctx->numArgs(); i++) {
		Value v = evalctx->getArgValue(i);
		stream << v.chrString();
	}
	return Value(stream.str());
}

Value builtin_ord(const Context *ctx, const EvalContext *evalctx)
{
	const size_t numArgs = evalctx->numArgs();

	if (numArgs == 0) {
		return Value();
	} else if (numArgs > 1) {
		PRINTB("WARNING: ord() called with %d arguments, only 1 argument expected, %s", numArgs % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}

	Value arg = evalctx->getArgValue(0);
	const str_utf8_wrapper &arg_str = arg.toStrUtf8Wrapper();
	const char *ptr = arg_str.c_str();

	if (arg.type() != Value::ValueType::STRING) {
		PRINTB("WARNING: ord() argument %s is not of type string, %s", arg_str.toString() % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}

	if (!g_utf8_validate(ptr, -1, NULL)) {
		PRINTB("WARNING: ord() argument '%s' is not valid utf8 string, %s", arg_str.toString() % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}

	if (arg_str.get_utf8_strlen() == 0) {
		return Value();
	}
	const gunichar ch = g_utf8_get_char(ptr);
	return Value((double)ch);
}

Value builtin_concat(const Context *, const EvalContext *evalctx)
{
	Value::VectorPtr result;

	for (size_t i = 0; i < evalctx->numArgs(); i++) {
		Value val = evalctx->getArgValue(i);
		if (val.type() == Value::ValueType::VECTOR) {
			for(const auto &v : *val.toVectorPtr()) {
				result->push_back(v.clone());
			}
		} else {
			result->push_back(std::move(val));
		}
	}
	return Value(result);
}

Value builtin_lookup(const Context *ctx, const EvalContext *evalctx)
{
	double p, low_p, low_v, high_p, high_v;
	if (evalctx->numArgs() != 2){ // Needs two args
		print_argCnt_warning("lookup", ctx, evalctx);
		return Value();
	}
	if(!evalctx->getArgValue(0).getDouble(p) || !std::isfinite(p)){ // First arg must be a number
		PRINTB("WARNING: lookup(%s, ...) first argument is not a number, %s", evalctx->getArgValue(0).toEchoString() % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}

	Value v1 = evalctx->getArgValue(1);
	const Value::VectorPtr &vec = v1.toVectorPtr();
	if (vec->empty()) return Value(); // Second must be a vector
	if (vec[0].toVectorPtr()->size() < 2) return Value(); // ..of vectors

	if (!vec[0].getVec2(low_p, low_v) || !vec[0].getVec2(high_p, high_v))
		return Value();
	for (size_t i = 1; i < vec->size(); i++) {
		double this_p, this_v;
		if (vec[i].getVec2(this_p, this_v)) {
			if (this_p <= p && (this_p > low_p || low_p > p)) {
				low_p = this_p;
				low_v = this_v;
			}
			if (this_p >= p && (this_p < high_p || high_p < p)) {
				high_p = this_p;
				high_v = this_v;
			}
		}
	}
	if (p <= low_p)
		return Value(high_v);
	if (p >= high_p)
		return Value(low_v);
	double f = (p-low_p) / (high_p-low_p);
	return Value(high_v * f + low_v * (1-f));
}

/*
 Pattern:

  "search" "(" ( match_value | list_of_match_values ) "," vector_of_vectors
        ("," num_returns_per_match
          ("," index_col_num )? )?
        ")";
  match_value : ( Value::ValueType::NUMBER | Value::ValueType::STRING );
  list_of_values : "[" match_value ("," match_value)* "]";
  vector_of_vectors : "[" ("[" Value ("," Value)* "]")+ "]";
  num_returns_per_match : int;
  index_col_num : int;

 The search string and searched strings can be unicode strings.
 Examples:
  Index values return as list:
    search("a","abcdabcd");
        - returns [0]
    search("Л","Л");  //A unicode string
        - returns [0]
    search("🂡aЛ","a🂡Л🂡a🂡Л🂡a",0);
        - returns [[1,3,5,7],[0,4,8],[2,6]]
    search("a","abcdabcd",0); //Search up to all matches
        - returns [[0,4]]
    search("a","abcdabcd",1);
        - returns [0]
    search("e","abcdabcd",1);
        - returns []
    search("a",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ]);
        - returns [0,4]

  Search on different column; return Index values:
    search(3,[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",3] ], 0, 1);
        - returns [0,8]

  Search on list of values:
    Return all matches per search vector element:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 0);
        - returns [[0,4],[1,5],[2,6]]

    Return first match per search vector element; special case return vector:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 1);
        - returns [0,1,2]

    Return first two matches per search vector element; vector of vectors:
      search("abce",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 2);
        - returns [[0,4],[1,5],[2,6],[8]]

*/

static Value::VectorPtr search(const str_utf8_wrapper &find, const str_utf8_wrapper &table,
																unsigned int num_returns_per_match,
																const Location &, const Context *)
{
	Value::VectorPtr returnvec;
	//Unicode glyph count for the length
	size_t findThisSize = find.get_utf8_strlen();
	size_t searchTableSize = table.get_utf8_strlen();
	for (size_t i = 0; i < findThisSize; i++) {
		unsigned int matchCount = 0;
		Value::VectorPtr resultvec;
		const gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
		for (size_t j = 0; j < searchTableSize; j++) {
			const gchar *ptr_st = g_utf8_offset_to_pointer(table.c_str(), j);
			if (ptr_ft && ptr_st && (g_utf8_get_char(ptr_ft) == g_utf8_get_char(ptr_st)) ) {
				matchCount++;
				if (num_returns_per_match == 1) {
					returnvec->emplace_back(double(j));
					break;
				} else {
					resultvec->emplace_back(double(j));
				}
				if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
					break;
				}
			}
		}
		if (matchCount == 0) {
			gchar utf8_of_cp[6] = ""; //A buffer for a single unicode character to be copied into
			if (ptr_ft) g_utf8_strncpy(utf8_of_cp, ptr_ft, 1);
		}
		if (num_returns_per_match == 0 || num_returns_per_match > 1) {
			returnvec->emplace_back(resultvec);
		}
	}
	return returnvec;
}

static Value::VectorPtr search(const str_utf8_wrapper &find, const Value::VectorPtr &table,
																unsigned int num_returns_per_match, unsigned int index_col_num, const Location &loc, const Context *ctx)
{
	Value::VectorPtr returnvec;
	//Unicode glyph count for the length
	unsigned int findThisSize =  find.get_utf8_strlen();
	unsigned int searchTableSize = table->size();
	for (size_t i = 0; i < findThisSize; i++) {
		unsigned int matchCount = 0;
		Value::VectorPtr resultvec;
		const gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
		for (size_t j = 0; j < searchTableSize; j++) {
			const Value::VectorPtr &entryVec = table[j].toVectorPtr();
			if (entryVec->size() <= index_col_num) {
				PRINTB("WARNING: Invalid entry in search vector at index %d, required number of values in the entry: %d. Invalid entry: %s, %s", j % (index_col_num + 1) % table[j].toEchoString() % loc.toRelativeString(ctx->documentPath()));
				return Value::VectorPtr();
			}
			const gchar *ptr_st = g_utf8_offset_to_pointer(entryVec[index_col_num].toString().c_str(), 0);
			if (ptr_ft && ptr_st && (g_utf8_get_char(ptr_ft) == g_utf8_get_char(ptr_st)) ) {
				matchCount++;
				if (num_returns_per_match == 1) {
					returnvec->emplace_back(double(j));
					break;
				} else {
					resultvec->emplace_back(double(j));
				}
				if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
					break;
				}
			}
		}
		if (matchCount == 0) {
			gchar utf8_of_cp[6] = ""; //A buffer for a single unicode character to be copied into
			if (ptr_ft) g_utf8_strncpy(utf8_of_cp, ptr_ft, 1);
			PRINTB("  WARNING: search term not found: \"%s\", %s", utf8_of_cp % loc.toRelativeString(ctx->documentPath()));
		}
		if (num_returns_per_match == 0 || num_returns_per_match > 1) {
			returnvec->emplace_back(resultvec);
		}
	}
	return returnvec;
}

Value builtin_search(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() < 2){
		print_argCnt_warning("search", ctx, evalctx);
		return Value();
	}

	Value findThis = evalctx->getArgValue(0);
	Value searchTable = evalctx->getArgValue(1);
	unsigned int num_returns_per_match = (evalctx->numArgs() > 2) ? (unsigned int)evalctx->getArgValue(2).toDouble() : 1;
	unsigned int index_col_num = (evalctx->numArgs() > 3) ? (unsigned int)evalctx->getArgValue(3).toDouble() : 0;

	Value::VectorPtr returnvec;

	if (findThis.type() == Value::ValueType::NUMBER) {
		unsigned int matchCount = 0;
		for (size_t j = 0; j < searchTable.toVectorPtr()->size(); j++) {
			Value search_element = searchTable.toVectorPtr()[j].clone();

			if ((index_col_num == 0 && findThis == search_element) ||
					(index_col_num < search_element.toVectorPtr()->size() &&
					 findThis      == search_element.toVectorPtr()[index_col_num])) {
				returnvec->emplace_back(double(j));
				matchCount++;
				if (num_returns_per_match != 0 && matchCount >= num_returns_per_match) break;
			}
		}
	} else if (findThis.type() == Value::ValueType::STRING) {
		if (searchTable.type() == Value::ValueType::STRING) {
			returnvec = search(findThis.toStrUtf8Wrapper(), searchTable.toStrUtf8Wrapper(), num_returns_per_match, evalctx->loc, ctx);
		}
		else {
			returnvec = search(findThis.toStrUtf8Wrapper(), searchTable.toVectorPtr(), num_returns_per_match, index_col_num, evalctx->loc, ctx);
		}
	} else if (findThis.type() == Value::ValueType::VECTOR) {
		const Value::VectorPtr &findVec = findThis.toVectorPtr();
		for (size_t i = 0; i < findVec->size(); i++) {
		  unsigned int matchCount = 0;
			Value::VectorPtr resultvec;

			Value find_value = findVec[i].clone();
 			const Value::VectorPtr &localTable = searchTable.toVectorPtr();	
			for (size_t j = 0; j < localTable->size(); j++) {

				Value search_element = localTable[j].clone();

				if ((index_col_num == 0 && find_value == search_element) ||
						(index_col_num < search_element.toVectorPtr()->size() &&
						 find_value   == search_element.toVectorPtr()[index_col_num])) {
					matchCount++;
					if (num_returns_per_match == 1) {
						returnvec->emplace_back(double(j));
						break;
					} else {
						resultvec->emplace_back(double(j));
					}
					if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) break;
				}
			}
			if (num_returns_per_match == 1 && matchCount == 0) {
				returnvec->emplace_back(resultvec);
			}
			if (num_returns_per_match == 0 || num_returns_per_match > 1) {
				returnvec->emplace_back(resultvec);
			}
		}
	} else {
		return Value();
	}
	return Value(returnvec);
}

#define QUOTE(x__) # x__
#define QUOTED(x__) QUOTE(x__)

Value builtin_version(const Context *, const EvalContext *evalctx)
{
	(void)evalctx; // unusued parameter
	Value::VectorPtr vec;
	vec->emplace_back(double(OPENSCAD_YEAR));
	vec->emplace_back(double(OPENSCAD_MONTH));
#ifdef OPENSCAD_DAY
	vec->emplace_back(double(OPENSCAD_DAY));
#endif
	return Value(vec);
}

Value builtin_version_num(const Context *ctx, const EvalContext *evalctx)
{
	Value val = (evalctx->numArgs() == 0) ? builtin_version(ctx, evalctx) : evalctx->getArgValue(0);
	double y, m, d;
	if (!val.getVec3(y, m, d, 0)) {
		return Value();
	}
	return Value(y * 10000 + m * 100 + d);
}

Value builtin_parent_module(const Context *ctx, const EvalContext *evalctx)
{
	int n;
	double d;
	int s = UserModule::stack_size();
	if (evalctx->numArgs() == 0)
		d=1; // parent module
	else if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() != Value::ValueType::NUMBER) return Value();
		v.getDouble(d);
	} else {
		print_argCnt_warning("parent_module", ctx, evalctx);
		return Value();
	}
	n=trunc(d);
	if (n < 0) {
		PRINTB("WARNING: Negative parent module index (%d) not allowed, %s", n % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}
	if (n >= s) {
		PRINTB("WARNING: Parent module index (%d) greater than the number of modules on the stack, %s", n % evalctx->loc.toRelativeString(ctx->documentPath()));
		return Value();
	}
	return Value(UserModule::stack_element(s - 1 - n));
}

Value builtin_norm(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		 Value val = evalctx->getArgValue(0);
		if (val.type() == Value::ValueType::VECTOR) {
			double sum = 0;
			const Value::VectorPtr &v = val.toVectorPtr();
			size_t n = v->size();
			for (size_t i = 0; i < n; i++)
				if (v[i].type() == Value::ValueType::NUMBER) {
					// sum += pow(v[i].toDouble(),2);
					double x = v[i].toDouble();
					sum += x*x;
				} else {
					PRINTB("WARNING: Incorrect arguments to norm(), %s", evalctx->loc.toRelativeString(ctx->documentPath()));
					return Value();
				}
			return Value(sqrt(sum));
		}
	}else{
		print_argCnt_warning("norm", ctx, evalctx);
	}
	return Value();
}

Value builtin_cross(const Context *ctx, const EvalContext *evalctx)
{
	auto loc = evalctx->loc;
	if (evalctx->numArgs() != 2) {
		PRINTB("WARNING: Invalid number of parameters for cross(), %s", loc.toRelativeString(ctx->documentPath()));
		return Value();
	}
	
	Value arg0 = evalctx->getArgValue(0);
	Value arg1 = evalctx->getArgValue(1);
	if ((arg0.type() != Value::ValueType::VECTOR) || (arg1.type() != Value::ValueType::VECTOR)) {
		PRINTB("WARNING: Invalid type of parameters for cross(), %s", loc.toRelativeString(ctx->documentPath()));
		return Value();
	}
	
	const Value::VectorPtr &v0 = arg0.toVectorPtr();
	const Value::VectorPtr &v1 = arg1.toVectorPtr();
	if ((v0->size() == 2) && (v1->size() == 2)) {
		return Value(v0[0].toDouble() * v1[1].toDouble() - v0[1].toDouble() * v1[0].toDouble());
	}

	if ((v0->size() != 3) || (v1->size() != 3)) {
		PRINTB("WARNING: Invalid vector size of parameter for cross(), %s", loc.toRelativeString(ctx->documentPath()));
		return Value();
	}
	for (unsigned int a = 0;a < 3;a++) {
		if ((v0[a].type() != Value::ValueType::NUMBER) || (v1[a].type() != Value::ValueType::NUMBER)) {
			PRINTB("WARNING: Invalid value in parameter vector for cross(), %s", loc.toRelativeString(ctx->documentPath()));
			return Value();
		}
		double d0 = v0[a].toDouble();
		double d1 = v1[a].toDouble();
		if (std::isnan(d0) || std::isnan(d1)) {
			PRINTB("WARNING: Invalid value (NaN) in parameter vector for cross(), %s", loc.toRelativeString(ctx->documentPath()));
			return Value();
		}
		if (std::isinf(d0) || std::isinf(d1)) {
			PRINTB("WARNING: Invalid value (INF) in parameter vector for cross(), %s", loc.toRelativeString(ctx->documentPath()));
			return Value();
		}
	}
	
	double x = v0[1].toDouble() * v1[2].toDouble() - v0[2].toDouble() * v1[1].toDouble();
	double y = v0[2].toDouble() * v1[0].toDouble() - v0[0].toDouble() * v1[2].toDouble();
	double z = v0[0].toDouble() * v1[1].toDouble() - v0[1].toDouble() * v1[0].toDouble();
	
	Value::VectorPtr result;
	result->emplace_back(x);
	result->emplace_back(y);
	result->emplace_back(z);
	return Value(result);
}

Value builtin_is_undef(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const auto &arg =evalctx->getArgs()[0];
		Value v;
		if(auto lookup = dynamic_pointer_cast<Lookup> (arg.expr)){
			return Value(lookup->evaluateSilently(evalctx).isUndefined());
		}else{
			return Value(evalctx->getArgValue(0).isUndefined());
		}
		return Value(v.isUndefined());
	}else{
		print_argCnt_warning("is_undef", ctx, evalctx);
	}

	return Value();
}

Value builtin_is_list(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::VECTOR){
			return Value(true);
		}else{
			return Value(false);
		}
	}else{
		print_argCnt_warning("is_list", ctx, evalctx);
	}
	return Value();
}

Value builtin_is_num(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::NUMBER){
			return Value(!std::isnan(v.toDouble()));
		}else{
			return Value(false);
		}
	}else{
		print_argCnt_warning("is_num", ctx, evalctx);
	}
	return Value();
}

Value builtin_is_bool(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::BOOL){
			return Value(true);
		}else{
			return Value(false);
		}
	}else{
		print_argCnt_warning("is_bool", ctx, evalctx);
	}
	return Value();
}

Value builtin_is_string(const Context *ctx, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		Value v = evalctx->getArgValue(0);
		if (v.type() == Value::ValueType::STRING){
			return Value(true);
		}else{
			return Value(false);
		}
	}else{
		print_argCnt_warning("is_string", ctx, evalctx);
	}
	return Value();
}

void register_builtin_functions()
{
	Builtins::init("abs", new BuiltinFunction(&builtin_abs),
				{
					"abs(number) -> number",
				});

	Builtins::init("sign", new BuiltinFunction(&builtin_sign),
				{
					"sign(number) -> -1, 0 or 1",
				});

	Builtins::init("rands", new BuiltinFunction(&builtin_rands),
				{
					"rands(min, max, num_results) -> vector",
					"rands(min, max, num_results, seed) -> vector",
				});

	Builtins::init("min", new BuiltinFunction(&builtin_min),
				{
					"min(number, number, ...) -> number",
					"min(vector) -> number",
				});

	Builtins::init("max", new BuiltinFunction(&builtin_max),
				{
					"max(number, number, ...) -> number",
					"max(vector) -> number",
				});

	Builtins::init("sin", new BuiltinFunction(&builtin_sin),
				{
					"sin(degrees) -> number",
				});

	Builtins::init("cos", new BuiltinFunction(&builtin_cos),
				{
					"cos(degrees) -> number",
				});

	Builtins::init("asin", new BuiltinFunction(&builtin_asin),
				{
					"asin(number) -> degrees",
				});

	Builtins::init("acos", new BuiltinFunction(&builtin_acos),
				{
					"acos(number) -> degrees",
				});

	Builtins::init("tan", new BuiltinFunction(&builtin_tan),
				{
					"tan(number) -> degrees",
				});

	Builtins::init("atan", new BuiltinFunction(&builtin_atan),
				{
					"atan(number) -> degrees",
				});

	Builtins::init("atan2", new BuiltinFunction(&builtin_atan2),
				{
					"atan2(number) -> degrees",
				});

	Builtins::init("round", new BuiltinFunction(&builtin_round),
				{
					"round(number) -> number",
				});

	Builtins::init("ceil", new BuiltinFunction(&builtin_ceil),
				{
					"ceil(number) -> number",
				});

	Builtins::init("floor", new BuiltinFunction(&builtin_floor),
				{
					"floor(number) -> number",
				});

	Builtins::init("pow", new BuiltinFunction(&builtin_pow),
				{
					"pow(base, exponent) -> number",
				});

	Builtins::init("sqrt", new BuiltinFunction(&builtin_sqrt),
				{
					"sqrt(number) -> number",
				});

	Builtins::init("exp", new BuiltinFunction(&builtin_exp),
				{
					"exp(number) -> number",
				});

	Builtins::init("len", new BuiltinFunction(&builtin_length),
				{
					"len(string) -> number",
					"len(vector) -> number",
				});

	Builtins::init("log", new BuiltinFunction(&builtin_log),
				{
					"log(number) -> number",
				});

	Builtins::init("ln", new BuiltinFunction(&builtin_ln),
				{
					"ln(number) -> number",
				});

	Builtins::init("str", new BuiltinFunction(&builtin_str),
				{
					"str(number or string, ...) -> string",
				});

	Builtins::init("chr", new BuiltinFunction(&builtin_chr),
				{
					"chr(number) -> string",
					"chr(vector) -> string",
					"chr(range) -> string",
				});

	Builtins::init("ord", new BuiltinFunction(&builtin_ord),
				{
					"ord(string) -> number",
				});

	Builtins::init("concat", new BuiltinFunction(&builtin_concat),
				{
					"concat(number or string or vector, ...) -> vector",
				});

	Builtins::init("lookup", new BuiltinFunction(&builtin_lookup),
				{
					"lookup(key, <key,value> vector) -> value",
				});

	Builtins::init("search", new BuiltinFunction(&builtin_search),
				{
					"search(string , string or vector [, num_returns_per_match [, index_col_num ] ] ) -> vector",
				});

	Builtins::init("version", new BuiltinFunction(&builtin_version),
				{
					"version() -> vector",
				});

	Builtins::init("version_num", new BuiltinFunction(&builtin_version_num),
				{
					"version_num() -> number",
				});

	Builtins::init("norm", new BuiltinFunction(&builtin_norm),
				{
					"norm(vector) -> number",
				});

	Builtins::init("cross", new BuiltinFunction(&builtin_cross),
				{
					"cross(vector, vector) -> vector",
				});

	Builtins::init("parent_module", new BuiltinFunction(&builtin_parent_module),
				{
					"parent_module(number) -> string",
				});

	Builtins::init("is_undef", new BuiltinFunction(&builtin_is_undef),
				{
					"is_undef(arg) -> boolean",
				});

	Builtins::init("is_list", new BuiltinFunction(&builtin_is_list),
				{
					"is_list(arg) -> boolean",
				});

	Builtins::init("is_num", new BuiltinFunction(&builtin_is_num),
				{
					"is_num(arg) -> boolean",
				});

	Builtins::init("is_bool", new BuiltinFunction(&builtin_is_bool),
				{
					"is_bool(arg) -> boolean",
				});

	Builtins::init("is_string", new BuiltinFunction(&builtin_is_string),
				{
					"is_string(arg) -> boolean",
				});
}
