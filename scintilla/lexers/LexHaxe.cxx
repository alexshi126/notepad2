// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Haxe.

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

// https://haxe.org/manual/std-String-literals.html
struct EscapeSequence {
	int outerState = SCE_HAXE_DEFAULT;
	int digitsLeft = 0;
	bool hex = false;
	bool brace = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		digitsLeft = 1;
		hex = true;
		brace = false;
		if (chNext == 'u') {
			digitsLeft = 5;
		} else if (chNext == 'x') {
			digitsLeft = 3;
		} else if (IsOctalDigit(chNext)) {
			digitsLeft = 3;
			hex = false;
		}
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsOctalOrHex(ch, hex);
	}
};

enum {
	HaxeLineStateMaskLineComment = 1, // line comment
	HaxeLineStateMaskImport = 1 << 1, // import
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Preprocessor = 1,
	KeywordIndex_Class = 2,
	KeywordIndex_Interface = 3,
	KeywordIndex_Enumeration = 4,
	KeywordIndex_Constant = 5,
	KeywordIndex_Metadata = 6,
	KeywordIndex_Function = 7,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_HAXE_DEFAULT,
	Class = SCE_HAXE_CLASS,
	Interface = SCE_HAXE_INTERFACE,
	Enum = SCE_HAXE_ENUM,
	Function = SCE_HAXE_FUNCTION_DEFINITION,
};

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_HAXE_STRINGSQ);

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_HAXE_TASKMARKER;
}

void ColouriseHaxeDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineType = 0;
	bool insideRegexRange = false; // inside regex character range []

	KeywordType kwType = KeywordType::None;
	int chBeforeIdentifier = 0;
	std::vector<int> nestedState; // string interpolation "${}"

	int visibleChars = 0;
	int visibleCharsBefore = 0;
	int chPrevNonWhite = 0;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		2: lineStateLineType
		3: nestedState count
		3*4: nestedState
		*/
		lineState >>= 8;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_HAXE_OPERATOR:
		case SCE_HAXE_OPERATOR2:
			sc.SetState(SCE_HAXE_DEFAULT);
			break;

		case SCE_HAXE_NUMBER:
			if (!IsDecimalNumberEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_HAXE_DEFAULT);
			}
			break;

		case SCE_HAXE_IDENTIFIER:
		case SCE_HAXE_MATADATA:
		case SCE_HAXE_VARIABLE:
		case SCE_HAXE_VARIABLE2:
			if (!IsIdentifierCharEx(sc.ch)) {
				switch (sc.state) {
				case SCE_HAXE_VARIABLE2:
					sc.SetState(escSeq.outerState);
					continue;

				case SCE_HAXE_MATADATA:
					if (sc.ch == '.') {
						sc.SetState(SCE_HAXE_OPERATOR);
						sc.ForwardSetState(SCE_HAXE_MATADATA);
						continue;
					}
					break;

				case SCE_HAXE_IDENTIFIER: {
					char s[128];
					sc.GetCurrent(s, sizeof(s));
					if (s[0] == '#') {
						if (keywordLists[KeywordIndex_Preprocessor].InList(s + 1)) {
							sc.ChangeState(SCE_HAXE_PREPROCESSOR);
						}
					} else if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						sc.ChangeState(SCE_HAXE_WORD);
						if (StrEqual(s, "import")) {
							if (visibleChars == sc.LengthCurrent()) {
								lineStateLineType = HaxeLineStateMaskImport;
							}
						} else if (StrEqualsAny(s, "class", "new", "extends", "abstract", "typedef")) {
							if (kwType != KeywordType::Enum) {
								kwType = KeywordType::Class;
							}
						} else if (StrEqualsAny(s, "interface", "implements")) {
							kwType = KeywordType::Interface;
						} else if (StrEqual(s, "enum")) {
							kwType = KeywordType::Enum;
						} else if (StrEqual(s, "function")) {
							kwType = KeywordType::Function;
						}
						if (kwType != KeywordType::None) {
							const int chNext = sc.GetDocNextChar();
							if (!IsIdentifierStartEx(chNext)) {
								kwType = KeywordType::None;
							}
						}
					} else if (keywordLists[KeywordIndex_Class].InList(s)) {
						sc.ChangeState(SCE_HAXE_CLASS);
					} else if (keywordLists[KeywordIndex_Interface].InList(s)) {
						sc.ChangeState(SCE_HAXE_INTERFACE);
					} else if (keywordLists[KeywordIndex_Enumeration].InList(s)) {
						sc.ChangeState(SCE_HAXE_ENUM);
					} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
						sc.ChangeState(SCE_HAXE_CONSTANT);
					} else if (sc.ch != '.') {
						if (kwType != KeywordType::None) {
							sc.ChangeState(static_cast<int>(kwType));
						} else {
							const int chNext = sc.GetDocNextChar();
							if (chNext == '(') {
								sc.ChangeState(SCE_HAXE_FUNCTION);
							} else if (sc.Match('[', ']')
								|| (chBeforeIdentifier == '<' && (chNext == '>' || chNext == '<'))) {
								// type[]
								// type<type>
								// type<type<type>>
								// type<type, type>
								sc.ChangeState(SCE_HAXE_CLASS);
							}
						}
					}
					if (sc.state != SCE_HAXE_WORD && sc.ch != '.') {
						kwType = KeywordType::None;
					}
				} break;
				}
				sc.SetState(SCE_HAXE_DEFAULT);
			}
			break;

		case SCE_HAXE_STRINGDQ:
		case SCE_HAXE_STRINGSQ:
			if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_HAXE_ESCAPECHAR);
					sc.Forward();
					if (sc.Match('u', '{')) {
						escSeq.brace = true;
						escSeq.digitsLeft = 7; // Unicode escape
						sc.Forward();
					}
				}
			} else if (sc.ch == '$' && sc.state == SCE_HAXE_STRINGSQ) {
				if (sc.chNext == '{') {
					nestedState.push_back(sc.state);
					sc.SetState(SCE_HAXE_OPERATOR2);
					sc.Forward();
				} else if (IsIdentifierStartEx(sc.chNext)) {
					escSeq.outerState = sc.state;
					sc.SetState(SCE_HAXE_VARIABLE2);
				}
			} else if (sc.ch == ((sc.state == SCE_HAXE_STRINGDQ) ? '"' : '\'')) {
				sc.ForwardSetState(SCE_HAXE_DEFAULT);
			}
			break;

		case SCE_HAXE_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				if (escSeq.brace && sc.ch == '}') {
					sc.Forward();
				}
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_HAXE_REGEX:
			if (sc.ch == '\\') {
				sc.Forward();
			} else if (sc.ch == '[' || sc.ch == ']') {
				insideRegexRange = sc.ch == '[';
			} else if (sc.ch == '/' && !insideRegexRange) {
				sc.Forward();
				// regex flags
				while (IsLowerCase(sc.ch)) {
					sc.Forward();
				}
				sc.SetState(SCE_HAXE_DEFAULT);
			}
			break;

		case SCE_HAXE_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_HAXE_DEFAULT);
			} else {
				HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_HAXE_TASKMARKER);
			}
			break;

		case SCE_HAXE_COMMENTBLOCK:
		case SCE_HAXE_COMMENTBLOCKDOC:
			if (sc.Match('*', '/')) {
				sc.Forward();
				sc.ForwardSetState(SCE_HAXE_DEFAULT);
			} else if (sc.state == SCE_HAXE_COMMENTBLOCKDOC && sc.ch == '@' && IsAlpha(sc.chNext) && IsCommentTagPrev(sc.chPrev)) {
				sc.SetState(SCE_HAXE_COMMENTTAGAT);
			} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_HAXE_TASKMARKER)) {
				continue;
			}
			break;

		case SCE_HAXE_COMMENTTAGAT:
			if (!IsAlpha(sc.ch)) {
				sc.SetState(SCE_HAXE_COMMENTBLOCKDOC);
				continue;
			}
			break;
		}

		if (sc.state == SCE_HAXE_DEFAULT) {
			if (sc.Match('/', '/')) {
				visibleCharsBefore = visibleChars;
				sc.SetState(SCE_HAXE_COMMENTLINE);
				if (visibleChars == 0) {
					lineStateLineType = HaxeLineStateMaskLineComment;
				}
			} else if (sc.Match('/', '*')) {
				visibleCharsBefore = visibleChars;
				sc.SetState(SCE_HAXE_COMMENTBLOCK);
				sc.Forward(2);
				if (sc.ch == '*' && sc.chNext != '*') {
					sc.ChangeState(SCE_HAXE_COMMENTBLOCKDOC);
				}
				continue;
			} else if (sc.Match('~', '/')) {
				insideRegexRange = false;
				sc.SetState(SCE_HAXE_REGEX);
				sc.Forward();
			} else if ( sc.ch == '\'') {
				sc.SetState(SCE_HAXE_STRINGSQ);
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_HAXE_STRINGDQ);
			} else if (IsNumberStartEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_HAXE_NUMBER);
			} else if (IsIdentifierStartEx(sc.ch) || (sc.ch == '#' && (sc.chNext == 'e' || sc.chNext == 'i'))) {
				if (chPrevNonWhite != '.') {
					chBeforeIdentifier = chPrevNonWhite;
				}
				sc.SetState(SCE_HAXE_IDENTIFIER);
			} else if (sc.ch == '@' && (sc.chNext == ':' || IsIdentifierStartEx(sc.chNext))) {
				sc.SetState(SCE_HAXE_MATADATA);
				if (sc.chNext == ':') {
					sc.Forward();
				}
			} else if (sc.ch == '$' && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_HAXE_VARIABLE);
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_HAXE_OPERATOR);
				if (!nestedState.empty()) {
					if (sc.ch == '{') {
						nestedState.push_back(SCE_HAXE_DEFAULT);
					} else if (sc.ch == '}') {
						const int outerState = TakeAndPop(nestedState);
						if (outerState != SCE_HAXE_DEFAULT) {
							sc.ChangeState(SCE_HAXE_OPERATOR2);
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
			int lineState = lineStateLineType;
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
		lineComment(lineState & HaxeLineStateMaskLineComment),
		packageImport((lineState >> 1) & 1) {
	}
};

constexpr bool IsStreamCommentStyle(int style) noexcept {
	return style == SCE_HAXE_COMMENTBLOCK
		|| style == SCE_HAXE_COMMENTBLOCKDOC
		|| style == SCE_HAXE_COMMENTTAGAT
		|| style == SCE_HAXE_TASKMARKER;
}

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_HAXE_STRINGDQ
		|| style == SCE_HAXE_STRINGSQ
		|| style == SCE_HAXE_OPERATOR2
		|| style == SCE_HAXE_VARIABLE2
		|| style == SCE_HAXE_ESCAPECHAR;
}

void FoldHaxeDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	FoldLineState foldPrev(0);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		foldPrev = FoldLineState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_HAXE_OPERATOR, SCE_HAXE_TASKMARKER, SCE_HAXE_PREPROCESSOR);
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
		case SCE_HAXE_COMMENTBLOCK:
		case SCE_HAXE_COMMENTBLOCKDOC:
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_HAXE_REGEX:
			if (style != stylePrev) {
				levelNext++;
			} else if (style != styleNext) {
				levelNext--;
			}
			break;

		case SCE_HAXE_STRINGSQ:
		case SCE_HAXE_STRINGDQ:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_HAXE_OPERATOR:
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
			break;

		case SCE_HAXE_PREPROCESSOR:
			if (ch == '#') {
				if (chNext == 'i' && styler[startPos + 1] == 'f') {
					levelNext++;
				} else if (chNext == 'e' && styler.Match(startPos, "end")) {
					levelNext--;
				}
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
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_HAXE_OPERATOR, SCE_HAXE_TASKMARKER, SCE_HAXE_PREPROCESSOR);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_HAXE_OPERATOR;
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

LexerModule lmHaxe(SCLEX_HAXE, ColouriseHaxeDoc, "haxe", FoldHaxeDoc);
