#include <sstream>
#include <ftl/either.h>
#include <ftl/functional.h>

#ifndef PARSER_GEN_H
#define PARSER_GEN_H

/**
 * Error reporting class.
 * 
 * We could have used a string directly, but this thin wrapper conveys more
 * semantic information to users of the library.
 */
class error {
public:
	// Default versions of c-tors default, copy, and move are acceptable

	/// Construct from string error message
	explicit error(const std::string& msg) noexcept : e(msg) {}
	explicit error(std::string&& msg) noexcept : e(std::move(msg)) {}

	/// Access the error message
	std::string message() const noexcept {
		return e;
	}

private:
	std::string e;
};

/// Convenience function to reduce template gibberish
template<typename T>
ftl::either<error,T> fail(const std::string& s) {
	return ftl::make_left<T>(error(s));
}

/// Convenience function to reduce template gibberish
template<typename T>
auto yield(T&& t) -> decltype(ftl::make_right<error>(std::forward<T>(t))) {
	return ftl::make_right<error>(std::forward<T>(t));
}

// Forward declarations required for later friend declarations, sigh
template<typename T> class parser;

template<typename T>
parser<T> lazy(ftl::function<parser<T>>);

template<typename T>
parser<T> lazy(parser<T>(*)());

/**
 * A parser of Ts.
 *
 * This is the central data type of the library.
 *
 * \par Concepts
 * \li Monad 
 * \li MonoidAlternative
 */
template<typename T>
class parser {
public:
	/* The basic library set of building blocks must be friended,
	 * as they all use private parts (c-tor).
	 */
	friend parser lazy<>(ftl::function<parser>);
	friend parser lazy<>(parser(*)());
	friend parser<char> anyChar();
	friend parser<char> parseChar(char);
	friend parser<char> notChar(char);
	friend parser<char> oneOf(std::string);
	friend parser<std::string> many(parser<char>);
	friend parser<std::string> many1(parser<char>);
	friend class ftl::monoidA<parser>;
	template<typename U> friend struct ftl::monad;

	using value_type = T;

	parser() = delete;
	parser(const parser&) = default;
	parser(parser&&) = default;
	~parser() = default;

	/**
	 * Run the parser, reading characters from some input stream.
	 */
	ftl::either<error,T> run(std::istream& s) const {
		return runP(s);
	}

private:
	using fn_t = ftl::function<ftl::either<error,T>,std::istream&>;
	explicit parser(fn_t f) : runP(f) {}

	fn_t runP;
};

namespace ftl {
	/**
	 * Monad instance for parsers.
	 *
	 * Also gives us Applicative and Functor.
	 */
	template<typename T>
	struct monad<parser<T>> {

		/**
		 * Consume no input, yield a.
		 */
		static parser<T> pure(T a) {
			return parser<T>{
				[a](std::istream& stream) {
					return yield(a);
				}
			};
		}

		/**
		 * Maps a function to the result of a parser.
		 *
		 * Can be a very useful combinator, f.ex. to apply smart constructors
		 * to the result of another parser.
		 */
		template<
				typename F,
				typename U = typename decayed_result<F(T)>::type>
		static parser<U> map(F f, parser<T> p) {
			return parser<U>([f,p](std::istream& s) {
				auto r = p.run(s);
				return f % r;
			});
		}

		/**
		 * Run two parsers in sequence, mapping output of p to f.
		 */
		template<
				typename F,
				typename U = typename decayed_result<F(T)>::type::value_type>
		static parser<U> bind(parser<T> p, F f) {
			return parser<U>([p,f](std::istream& strm) {

				auto r = p.run(strm);
				if(r) {
					parser<U> p2 = f(*r);
					return p2.run(strm);
				}

				else {
					return fail<U>(r.left().message());
				}
			});
		}

		// Yes, parser is an instance of monad (and applicative, and functor)
		static constexpr bool instance = true;
	};

	/**
	 * monoidA instance for parser.
	 *
	 * Parsers have a well defined fail state (when the either is of left
	 * type), so this concept makes perfect sense.
	 */
	template<>
	struct monoidA<parser> {
		/// Generic fail parser
		template<typename T>
		static parser<T> fail() {
			return parser<T>{
				[](std::istream&) {
					return fail<T>("Unknown parse error.");
				}
			};
		}

		/**
		 * Try two parsers in sequence.
		 *
		 * If p1 fails, then run p2. If both fail, then the composite parser
		 * fails.
		 *
		 * \note p1 could in some situations consume input and _then_ fail. This
		 *       might be exactly what you want, or it might be very confusing.
		 */
		template<typename T>
		static parser<T> orDo(parser<T> p1, parser<T> p2) {
			return parser<T>{[p1,p2](std::istream& is) {
				auto r = p1.run(is);
				if(r)
					return r;

				else {
					auto r2 = p2.run(is);
					if(r2)
						return r2;

					else {
						std::ostringstream oss;
						oss << r.left().message()
							<< " or " << r2.left().message();
						return ::fail<T>(oss.str());
					}
				}
			}};
		}

		static constexpr bool instance = true;
	};
}

/* What follows is a basic set of blocks that a user of the library can
 * combine with the various combinators available (operator||, monad instance,
 * applicative instance, functor instance).
 */

/**
 * Parses any one character.
 *
 * This parser can only fail if the end of stream has been reached.
 */
parser<char> anyChar();

/**
 * Parses one specific character.
 *
 * This parser will fail if the next character in the stream is not equal
 * to \c c.
 */
parser<char> parseChar(char c);

/**
 * Parses any character except c.
 *
 * This parser will fail if the next character \em does equal \c c.
 */
parser<char> notChar(char c);

/**
 * Parses one of the characters in str.
 *
 * This parser will fail if the next character in the stream does not appear
 * in str.
 */
parser<char> oneOf(std::string str);

/**
 * Greedily parses 0 or more of p.
 *
 * This parser cannot fail. If end of stream is reached or p fails on the
 * first run, the result will be an empty string.
 */
parser<std::string> many(parser<char> p);

/**
 * Greedily parses 1 or more of p.
 *
 * This parser will fail if the first attempt at parsing p fails.
 */
parser<std::string> many1(parser<char> p);

/**
 * Lazily run the parser generated by f
 *
 * This is useful e.g. if you want a parser to recurse.
 */
template<typename T>
parser<T> lazy(ftl::function<parser<T>> f) {
	return parser<T>([f](std::istream& is) {
		return f().run(is);
	});
}

/// \overload
template<typename T>
parser<T> lazy(parser<T>(*f)()) {
	return parser<T>([f](std::istream& is) {
			return f().run(is);
	});
}

#endif

