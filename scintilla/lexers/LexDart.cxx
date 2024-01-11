// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Dart.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"
#include "LexerUtils.h"

using namespace Lexilla;

namespace {

struct EscapeSequence {
	int outerState = SCE_DART_DEFAULT;
	int digitsLeft = 0;
	bool brace = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		brace = false;
		digitsLeft = (chNext == 'x')? 3 : ((chNext == 'u') ? 5 : 1);
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

enum {
	DartLineStateMaskLineComment = 1,	// line comment
	DartLineStateMaskImport = (1 << 1),	// import
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Type = 1,
	KeywordIndex_Class = 2,
	KeywordIndex_Enumeration = 3,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_DART_DEFAULT,
	Class = SCE_DART_CLASS,
	Enum = SCE_DART_ENUM,
	Label = SCE_DART_LABEL,
	Return = 0x40,
	While,
};

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_DART_STRING_SQ);
static_assert(DefaultNestedStateBaseStyle + 2 == SCE_DART_STRING_DQ);
static_assert(DefaultNestedStateBaseStyle + 3 == SCE_DART_TRIPLE_STRING_SQ);
static_assert(DefaultNestedStateBaseStyle + 4 == SCE_DART_TRIPLE_STRING_DQ);

constexpr bool IsDeclarableOperator(int ch) noexcept {
	// https://github.com/dart-lang/sdk/blob/main/sdk/lib/core/symbol.dart
	return AnyOf(ch, '+', '-', '*', '/', '%', '~', '&', '|',
					 '^', '<', '>', '=', '[', ']');
}

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_DART_TASKMARKER;
}

constexpr bool IsTripleString(int state) noexcept {
	return ((state - SCE_DART_STRING_SQ) & 3) > 1;
}

constexpr int GetStringQuote(int state) noexcept {
	if constexpr (SCE_DART_STRING_SQ & 1) {
		return (state & 1) ? '\'' : '\"';
	} else {
		return (state & 1) ? '\"' : '\'';
	}
}

void ColouriseDartDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineType = 0;
	int commentLevel = 0;	// nested block comment level

	KeywordType kwType = KeywordType::None;
	int chBeforeIdentifier = 0;

	std::vector<int> nestedState; // string interpolation "${}"

	int visibleChars = 0;
	int chBefore = 0;
	int visibleCharsBefore = 0;
	int chPrevNonWhite = 0;
	bool simpleStringInterpolation = false;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		2: lineStateLineType
		6: commentLevel
		3: nestedState count
		3*4: nestedState
		*/
		commentLevel = (lineState >> 2) & 0x3f;
		lineState >>= 8;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}
	if (startPos == 0) {
		if (sc.Match('#', '!')) {
			// Shell Shebang at beginning of file
			sc.SetState(SCE_DART_COMMENTLINE);
			sc.Forward();
			lineStateLineType = DartLineStateMaskLineComment;
		}
	} else if (IsSpaceEquiv(initStyle)) {
		LookbackNonWhite(styler, startPos, SCE_DART_TASKMARKER, chPrevNonWhite, initStyle);
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_DART_OPERATOR:
		case SCE_DART_OPERATOR2:
			sc.SetState(SCE_DART_DEFAULT);
			break;

		case SCE_DART_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_DART_DEFAULT);
			}
			break;

		case SCE_DART_IDENTIFIER:
		case SCE_DART_VARIABLE:
		case SCE_DART_VARIABLE2:
		case SCE_DART_METADATA:
		case SCE_DART_SYMBOL_IDENTIFIER:
			if (!IsIdentifierCharEx(sc.ch)) {
				switch (sc.state) {
				case SCE_DART_VARIABLE2:
					sc.SetState(escSeq.outerState);
					continue;

				case SCE_DART_METADATA:
				case SCE_DART_SYMBOL_IDENTIFIER:
					if (sc.ch == '.') {
						const int state = sc.state;
						sc.SetState(SCE_DART_OPERATOR);
						sc.ForwardSetState(state);
						continue;
					}
					break;

				case SCE_DART_IDENTIFIER: {
					char s[128];
					sc.GetCurrent(s, sizeof(s));
					if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						sc.ChangeState(SCE_DART_WORD);
						if (StrEqualsAny(s, "import", "part")) {
							if (visibleChars == sc.LengthCurrent()) {
								lineStateLineType = DartLineStateMaskImport;
							}
						} else if (StrEqualsAny(s, "class", "extends", "implements", "new", "throw", "with", "as", "is", "on")) {
							kwType = KeywordType::Class;
						} else if (StrEqual(s, "enum")) {
							kwType = KeywordType::Enum;
						} else if (StrEqualsAny(s, "break", "continue")) {
							kwType = KeywordType::Label;
						} else if (StrEqualsAny(s, "return", "await", "yield")) {
							kwType = KeywordType::Return;
						}
						if (kwType > KeywordType::None && kwType < KeywordType::Return) {
							const int chNext = sc.GetLineNextChar();
							if (!IsIdentifierStartEx(chNext)) {
								kwType = KeywordType::None;
							}
						}
					} else if (keywordLists[KeywordIndex_Type].InList(s)) {
						sc.ChangeState(SCE_DART_WORD2);
					} else if (keywordLists[KeywordIndex_Class].InList(s)) {
						sc.ChangeState(SCE_DART_CLASS);
					} else if (keywordLists[KeywordIndex_Enumeration].InList(s)) {
						sc.ChangeState(SCE_DART_ENUM);
					} else if (sc.ch == ':') {
						if (chBefore == ',' || chBefore == '{') {
							sc.ChangeState(SCE_DART_KEY);
						} else if (IsJumpLabelPrevChar(chBefore)) {
							sc.ChangeState(SCE_DART_LABEL);
						}
					} else if (sc.ch != '.') {
						if (kwType > KeywordType::None && kwType < KeywordType::Return) {
							sc.ChangeState(static_cast<int>(kwType));
						} else {
							const int chNext = sc.GetDocNextChar(sc.ch == '?');
							if (chNext == '(') {
								// type method()
								// type[] method()
								// type<type> method()
								if (kwType != KeywordType::Return && (IsIdentifierCharEx(chBefore) || chBefore == ']')) {
									sc.ChangeState(SCE_DART_FUNCTION_DEFINITION);
								} else {
									sc.ChangeState(SCE_DART_FUNCTION);
								}
							} else if ((chBeforeIdentifier == '<' && (chNext == '>' || chNext == '<'))
								|| IsIdentifierStartEx(chNext)) {
								// type<type>
								// type<type?>
								// type<type<type>>
								// type<type, type>
								// class type implements interface, interface {}
								// type identifier
								// type? identifier
								sc.ChangeState(SCE_DART_CLASS);
							}
						}
					}
					if (sc.state != SCE_DART_WORD && sc.ch != '.') {
						kwType = KeywordType::None;
					}
				} break;
				}

				sc.SetState(SCE_DART_DEFAULT);
			}
			break;

		case SCE_DART_SYMBOL_OPERATOR:
			if (!IsDeclarableOperator(sc.ch)) {
				sc.SetState(SCE_DART_DEFAULT);
			}
			break;

		case SCE_DART_COMMENTLINE:
		case SCE_DART_COMMENTLINEDOC:
			if (sc.atLineStart) {
				sc.SetState(SCE_DART_DEFAULT);
			} else {
				HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_DART_TASKMARKER);
			}
			break;

		case SCE_DART_COMMENTBLOCK:
		case SCE_DART_COMMENTBLOCKDOC:
			if (sc.Match('*', '/')) {
				sc.Forward();
				--commentLevel;
				if (commentLevel == 0) {
					sc.ForwardSetState(SCE_DART_DEFAULT);
				}
			} else if (sc.Match('/', '*')) {
				sc.Forward();
				++commentLevel;
			} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_DART_TASKMARKER)) {
				continue;
			}
			break;

		case SCE_DART_STRING_SQ:
		case SCE_DART_STRING_DQ:
		case SCE_DART_TRIPLE_STRING_SQ:
		case SCE_DART_TRIPLE_STRING_DQ:
		case SCE_DART_RAWSTRING_SQ:
		case SCE_DART_RAWSTRING_DQ:
		case SCE_DART_TRIPLE_RAWSTRING_SQ:
		case SCE_DART_TRIPLE_RAWSTRING_DQ:
			if (sc.atLineStart && !IsTripleString(sc.state)) {
				sc.SetState(SCE_DART_DEFAULT);
			} else if (sc.ch == '\\' && sc.state < SCE_DART_RAWSTRING_SQ) {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_DART_ESCAPECHAR);
					sc.Forward();
					if (sc.Match('u', '{')) {
						escSeq.brace = true;
						escSeq.digitsLeft = 7; // Unicode code point
						sc.Forward();
					}
				}
			} else if (sc.ch == '$' && sc.state < SCE_DART_RAWSTRING_SQ) {
				if (sc.chNext == '{' || sc.chNext == '(') {
					if (sc.chNext == '(') {
						simpleStringInterpolation = true;
						escSeq.outerState = sc.state;
					} else {
						nestedState.push_back(sc.state);
					}
					sc.SetState(SCE_DART_OPERATOR2);
					sc.Forward();
				} else if (IsIdentifierStartEx(sc.chNext)) {
					escSeq.outerState = sc.state;
					sc.SetState(SCE_DART_VARIABLE2);
				}
			} else if (sc.ch == GetStringQuote(sc.state) && (!IsTripleString(sc.state) || sc.MatchNext())) {
				if (IsTripleString(sc.state)) {
					sc.Advance(2);
				}
				sc.Forward();
				if (sc.state <= SCE_DART_STRING_DQ && (chBefore == ',' || chBefore == '{')) {
					const int chNext = sc.GetLineNextChar();
					if (chNext == ':') {
						sc.ChangeState(SCE_DART_KEY);
					}
				}
				sc.SetState(SCE_DART_DEFAULT);
			}
			break;

		case SCE_DART_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				if (escSeq.brace && sc.ch == '}') {
					sc.Forward();
				}
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;
		}

		if (sc.state == SCE_DART_DEFAULT) {
			if (sc.ch == '/' && (sc.chNext == '/' || sc.chNext == '*')) {
				visibleCharsBefore = visibleChars;
				const int chNext = sc.chNext;
				sc.SetState((chNext == '/') ? SCE_DART_COMMENTLINE : SCE_DART_COMMENTBLOCK);
				sc.Forward(2);
				if (sc.ch == chNext && sc.chNext != chNext) {
					sc.ChangeState((chNext == '/') ? SCE_DART_COMMENTLINEDOC : SCE_DART_COMMENTBLOCKDOC);
				}
				if (chNext == '/') {
					if (visibleChars == 0) {
						lineStateLineType = DartLineStateMaskLineComment;
					}
				 } else {
					commentLevel = 1;
				 }
				 continue;
			}
			if (sc.ch == 'r' && (sc.chNext == '\'' || sc.chNext == '"')) {
				sc.SetState((sc.chNext == '\'') ? SCE_DART_RAWSTRING_SQ : SCE_DART_RAWSTRING_DQ);
				sc.Forward(2);
				if (sc.chPrev == '\'' && sc.Match('\'', '\'')) {
					sc.ChangeState(SCE_DART_TRIPLE_RAWSTRING_SQ);
					sc.Forward(2);
				} else if (sc.chPrev == '"' && sc.Match('"', '"')) {
					sc.ChangeState(SCE_DART_TRIPLE_RAWSTRING_DQ);
					sc.Forward(2);
				}
				continue;
			}
			if (sc.ch == '"') {
				if (sc.MatchNext('"', '"')) {
					sc.SetState(SCE_DART_TRIPLE_STRING_DQ);
					sc.Advance(2);
				} else {
					chBefore = chPrevNonWhite;
					sc.SetState(SCE_DART_STRING_DQ);
				}
			} else if (sc.ch == '\'') {
				if (sc.MatchNext('\'', '\'')) {
					sc.SetState(SCE_DART_TRIPLE_STRING_SQ);
					sc.Advance(2);
				} else {
					chBefore = chPrevNonWhite;
					sc.SetState(SCE_DART_STRING_SQ);
				}
			} else if (IsNumberStart(sc.ch, sc.chNext)) {
				sc.SetState(SCE_DART_NUMBER);
			} else if ((sc.ch == '@' || sc.ch == '$') && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState((sc.ch == '@') ? SCE_DART_METADATA : SCE_DART_VARIABLE);
			} else if (sc.ch == '#') {
				if (IsIdentifierStartEx(sc.chNext)) {
					sc.SetState(SCE_DART_SYMBOL_IDENTIFIER);
				} else if (IsDeclarableOperator(sc.chNext)) {
					sc.SetState(SCE_DART_SYMBOL_OPERATOR);
				}
			} else if (IsIdentifierStartEx(sc.ch)) {
				chBefore = chPrevNonWhite;
				if (chPrevNonWhite != '.') {
					chBeforeIdentifier = chPrevNonWhite;
				}
				sc.SetState(SCE_DART_IDENTIFIER);
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_DART_OPERATOR);
				if (simpleStringInterpolation && sc.ch == ')') {
					simpleStringInterpolation = false;
					sc.ChangeState(SCE_DART_OPERATOR2);
					sc.ForwardSetState(escSeq.outerState);
					continue;
				}
				if (!nestedState.empty()) {
					if (sc.ch == '{') {
						nestedState.push_back(SCE_DART_DEFAULT);
					} else if (sc.ch == '}') {
						const int outerState = TakeAndPop(nestedState);
						if (outerState != SCE_DART_DEFAULT) {
							sc.ChangeState(SCE_DART_OPERATOR2);
						}
						sc.ForwardSetState(outerState);
						continue;
					}
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
			}
		}
		if (sc.atLineEnd) {
			int lineState = (commentLevel << 2) | lineStateLineType;
			if (!nestedState.empty()) {
				lineState |= PackLineState(nestedState) << 8;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineStateLineType = 0;
			visibleChars = 0;
			visibleCharsBefore = 0;
			kwType = KeywordType::None;
		}
		sc.Forward();
	}

	sc.Complete();
}

struct FoldLineState {
	int lineComment;
	int packageImport;
	constexpr explicit FoldLineState(int lineState) noexcept:
		lineComment(lineState & DartLineStateMaskLineComment),
		packageImport((lineState >> 1) & 1) {
	}
};

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_DART_TRIPLE_STRING_SQ
		|| style == SCE_DART_TRIPLE_STRING_DQ
		|| style == SCE_DART_OPERATOR2
		|| style == SCE_DART_VARIABLE2
		|| style == SCE_DART_ESCAPECHAR;
}

void FoldDartDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	FoldLineState foldPrev(0);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		foldPrev = FoldLineState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_DART_OPERATOR, SCE_DART_TASKMARKER);
		if (bracePos) {
			startPos = bracePos + 1; // skip the brace
		}
	}

	int levelNext = levelCurrent;
	FoldLineState foldCurrent(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	int visibleChars = 0;

	while (startPos < endPos) {
		const char ch = chNext;
		const int stylePrev = style;
		style = styleNext;
		chNext = styler[++startPos];
		styleNext = styler.StyleAt(startPos);

		switch (style) {
		case SCE_DART_COMMENTBLOCKDOC:
		case SCE_DART_COMMENTBLOCK: {
			const int level = (ch == '/' && chNext == '*') ? 1 : ((ch == '*' && chNext == '/') ? -1 : 0);
			if (level != 0) {
				levelNext += level;
				startPos++;
				chNext = styler[startPos];
				styleNext = styler.StyleAt(startPos);
			}
		} break;

		case SCE_DART_TRIPLE_RAWSTRING_SQ:
		case SCE_DART_TRIPLE_RAWSTRING_DQ:
			if (style != stylePrev) {
				levelNext++;
			} else if (style != styleNext) {
				levelNext--;
			}
			break;

		case SCE_DART_TRIPLE_STRING_SQ:
		case SCE_DART_TRIPLE_STRING_DQ:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_DART_OPERATOR:
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
			break;
		}

		if (visibleChars == 0 && !IsSpaceEquiv(style)) {
			++visibleChars;
		}
		if (startPos == lineStartNext) {
			const FoldLineState foldNext(styler.GetLineState(lineCurrent + 1));
			if (foldCurrent.lineComment) {
				levelNext += foldNext.lineComment - foldPrev.lineComment;
			} else if (foldCurrent.packageImport) {
				levelNext += foldNext.packageImport - foldPrev.packageImport;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_DART_OPERATOR, SCE_DART_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_DART_OPERATOR;
					chNext = styler[startPos];
					styleNext = styler.StyleAt(startPos);
				}
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			styler.SetLevel(lineCurrent, lev);

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineStartNext = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			foldPrev = foldCurrent;
			foldCurrent = foldNext;
			visibleChars = 0;
		}
	}
}

}

LexerModule lmDart(SCLEX_DART, ColouriseDartDoc, "dart", FoldDartDoc);
