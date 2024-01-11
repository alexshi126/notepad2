// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for WinHex script and template.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>

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

using namespace Lexilla;

namespace {

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Type = 1,
	KeywordIndex_Command = 2,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

void ColouriseWinHexDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const bool fold = styler.GetPropertyBool("fold");
	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (sc.currentLine > 0) {
		levelCurrent = styler.LevelAt(sc.currentLine - 1) >> 16;
	}
	int levelNext = levelCurrent;
	int visibleChars = 0;

	while (sc.More()) {
		switch (sc.state) {
		case SCE_WINHEX_OPERATOR:
			sc.SetState(SCE_WINHEX_DEFAULT);
			break;

		case SCE_WINHEX_NUMBER:
			if (!IsAlphaNumeric(sc.ch)) {
				sc.SetState(SCE_WINHEX_DEFAULT);
			}
			break;

		case SCE_WINHEX_IDENTIFIER:
			if (!IsIdentifierChar(sc.ch) && sc.ch != '-') {
				char s[64];
				sc.GetCurrentLowered(s, sizeof(s));
				if (keywordLists[KeywordIndex_Keyword].InList(s)) {
					sc.ChangeState(SCE_WINHEX_KEYWORD);
				} else if (keywordLists[KeywordIndex_Type].InList(s)) {
					sc.ChangeState(SCE_WINHEX_TYPE);
				} else if (keywordLists[KeywordIndex_Command].InList(s)) {
					sc.ChangeState(SCE_WINHEX_COMMAND);
				}
				if (sc.state != SCE_WINHEX_IDENTIFIER && visibleChars == sc.LengthCurrent()) {
					if (StrStartsWith(s, "if") || StrEqualsAny(s, "begin", "section")) {
						levelNext++;
					} else if (StrStartsWith(s, "end")) {
						levelNext--;
					}
				}
				sc.SetState(SCE_WINHEX_DEFAULT);
			}
			break;

		case SCE_WINHEX_STRING:
			if (sc.atLineStart) {
				sc.SetState(SCE_WINHEX_DEFAULT);
			} else if (sc.ch == '\"') {
				sc.ForwardSetState(SCE_WINHEX_DEFAULT);
			}
			break;

		case SCE_WINHEX_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_WINHEX_DEFAULT);
			}
			break;
		}

		if (sc.state == SCE_WINHEX_DEFAULT) {
			if (sc.Match('/', '/')) {
				sc.SetState(SCE_WINHEX_COMMENTLINE);
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_WINHEX_STRING);
			} else if (IsADigit(sc.ch)) {
				sc.SetState(SCE_WINHEX_NUMBER);
			} else if (IsIdentifierStart(sc.ch)) {
				sc.SetState(SCE_WINHEX_IDENTIFIER);
			} else if (IsAGraphic(sc.ch) && sc.ch != '\\') {
				sc.SetState(SCE_WINHEX_OPERATOR);
				if (sc.ch == '{') {
					++levelNext;
				} else if (sc.ch == '}') {
					--levelNext;
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
		}
		if (sc.atLineEnd) {
			visibleChars = 0;
			if (fold) {
				const int levelUse = levelCurrent;
				int lev = levelUse | levelNext << 16;
				if (levelUse < levelNext) {
					lev |= SC_FOLDLEVELHEADERFLAG;
				}
				styler.SetLevel(sc.currentLine, lev);
				levelCurrent = levelNext;
			}
		}
		sc.Forward();
	}

	sc.Complete();
}

}

LexerModule lmWinHex(SCLEX_WINHEX, ColouriseWinHexDoc, "winhex");
