/* This file is part of the KDE libraries
  Copyright (C) 2004 Joseph Wenninger <jowenn@kde.org>
  Copyright (C) 2009 Milian Wolff <mail@milianw.de>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License version 2 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*/

#include "katetemplatehandler.h"
#include "katedocument.h"
#include "katesmartcursor.h"
#include "kateview.h"
#include "kateconfig.h"
#include "katerenderer.h"

#include <ktexteditor/cursor.h>
#include <ktexteditor/smartcursor.h>
#include <ktexteditor/smartrange.h>
#include <ktexteditor/range.h>
#include <ktexteditor/attribute.h>

#include <QtCore/QQueue>

#include <kdebug.h>

using namespace KTextEditor;

/**
 * Debug function to dump the range with all it's children.
 */
void dumpRange(SmartRange* range, int depth = 0) {
  kDebug() << depth << range << *range << range->notifiers().size();
  foreach ( SmartRange* child, range->childRanges() ) {
    dumpRange( child, depth + 1 );
  }
}

/* ####################################### */

KateTemplateHandler::KateTemplateHandler( KateDocument *doc, const Cursor& position,
                                          const QString &templateString, const QMap<QString, QString> &initialValues )
    : QObject(doc)
    , m_doc(doc), m_wholeTemplateRange(0), m_finalCursorPosition(0)
    , m_lastCaretPosition(position), m_isMirroring(false), m_editWithUndo(false), m_jumping(false)
{
  kDebug() << templateString << initialValues;
  if ( !initialValues.isEmpty() ) {
    // only do complex stuff when required

    handleTemplateString(position, templateString, initialValues);

    if ( !m_templateRanges.isEmpty() ) {
      foreach ( View* view, m_doc->views() ) {
        setupEventHandler(view);
      }

      connect(m_doc, SIGNAL(viewCreated(KTextEditor::Document*, KTextEditor::View*)),
              this, SLOT(slotViewCreated(KTextEditor::Document*, KTextEditor::View*)));
      connect(m_doc, SIGNAL(textChanged(KTextEditor::Document*, KTextEditor::Range, KTextEditor::Range)),
              this, SLOT(slotTextChanged(KTextEditor::Document*, KTextEditor::Range)));
      connect(m_doc, SIGNAL(textInserted(KTextEditor::Document*, KTextEditor::Range)),
              this, SLOT(slotTextChanged(KTextEditor::Document*, KTextEditor::Range)));
      connect(m_doc, SIGNAL(textRemoved(KTextEditor::Document*, KTextEditor::Range)),
              this, SLOT(slotTextChanged(KTextEditor::Document*, KTextEditor::Range)));
    } else {
      // when no interesting ranges got added, we can terminate directly
      jumpToFinalCursorPosition();
      cleanupAndExit();
    }
  } else {
    // simple templates just need to be inserted
    insertText(position, templateString);
    cleanupAndExit();
  }
}

KateTemplateHandler::~KateTemplateHandler()
{
}

/// simple wrapper to delete a smartrange
void deleteSmartRange(SmartRange* range, KateDocument* doc) {
  doc->removeHighlightFromDocument(range);
  SmartCursor* start = &range->smartStart();
  SmartCursor* end = &range->smartEnd();
  doc->unbindSmartRange(range);
  delete start;
  delete end;
}

void KateTemplateHandler::cleanupAndExit()
{
  disconnect(m_doc, SIGNAL(viewCreated(KTextEditor::Document*,KTextEditor::View*)),
             this, SLOT(slotViewCreated(KTextEditor::Document*,KTextEditor::View*)));
  disconnect(m_doc, SIGNAL(textChanged(KTextEditor::Document*,KTextEditor::Range,KTextEditor::Range)),
             this, SLOT(slotTextChanged(KTextEditor::Document*,KTextEditor::Range)));
  disconnect(m_doc, SIGNAL(textInserted(KTextEditor::Document*,KTextEditor::Range)),
             this, SLOT(slotTextChanged(KTextEditor::Document*,KTextEditor::Range)));
  disconnect(m_doc, SIGNAL(textRemoved(KTextEditor::Document*,KTextEditor::Range)),
             this, SLOT(slotTextChanged(KTextEditor::Document*,KTextEditor::Range)));

  if ( !m_templateRanges.isEmpty() ) {
      foreach ( SmartRange* range, m_templateRanges ) {
        deleteSmartRange(range, m_doc);
      }
      m_templateRanges.clear();
  }
  if ( m_wholeTemplateRange ) {
    deleteSmartRange(m_wholeTemplateRange, m_doc);
  }
  if ( m_finalCursorPosition ) {
    delete m_finalCursorPosition;
  }
  delete this;
}

void KateTemplateHandler::jumpToFinalCursorPosition()
{
  if ( m_doc->activeView() ) {
    m_doc->activeView()->setSelection(Range::invalid());
    m_doc->activeView()->setCursorPosition(*m_finalCursorPosition);
  }
}

void KateTemplateHandler::setEditWithUndo(const bool &enabled)
{
  m_editWithUndo = enabled;
}

void KateTemplateHandler::slotViewCreated(Document* document, View* view)
{
  Q_ASSERT(document == m_doc);
  setupEventHandler(view);
}

void KateTemplateHandler::setupEventHandler(View* view)
{
  view->focusProxy()->installEventFilter(this);
}

bool KateTemplateHandler::eventFilter(QObject* object, QEvent* event)
{
  // prevent indenting by eating the keypress event for TAB
  if ( event->type() == QEvent::KeyPress ) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
    if ( keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab ) {
      if ( !m_doc->activeKateView()->isCompletionActive() ) {
        return true;
      }
    }
  }
  // actually offer shortcuts for navigation
  if ( event->type() == QEvent::ShortcutOverride ) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
    if ( keyEvent->key() == Qt::Key_Return && keyEvent->modifiers() & Qt::AltModifier ) {
      // terminate
      jumpToFinalCursorPosition();
      cleanupAndExit();
      return true;
    } else if ( keyEvent->key() == Qt::Key_Escape ) {
      if ( !m_doc->activeView() || !m_doc->activeView()->selection() ) {
        // terminate
        jumpToFinalCursorPosition();
        cleanupAndExit();
        return true;
      }
    } else if ( keyEvent->key() == Qt::Key_Tab ) {
      if ( keyEvent->modifiers() & Qt::Key_Shift ) {
        jumpToPreviousRange();
      } else {
        jumpToNextRange();
      }
      return true;
    } else if ( keyEvent->key() == Qt::Key_Backtab ) {
      jumpToPreviousRange();
      return true;
    }
  }
  return QObject::eventFilter(object, event);
}

void KateTemplateHandler::jumpToPreviousRange()
{
  const Cursor & cursor = m_doc->activeView()->cursorPosition();
  if ( cursor == *m_finalCursorPosition ) {
    // wrap and jump to last range
    setCurrentRange(m_templateRanges.last());
    return;
  }
  SmartRange* previousRange = 0;
  foreach ( SmartRange* range, m_templateRanges ) {
    if ( range->start() >= cursor ) {
      continue;
    }
    if ( !previousRange || range->start() > previousRange->start() ) {
      previousRange = range;
    }
  }
  kDebug() << previousRange << *previousRange;
  if ( previousRange ) {
    setCurrentRange(previousRange);
  } else {
    // wrap and jump to final cursor
    jumpToFinalCursorPosition();
  }
}

void KateTemplateHandler::jumpToNextRange()
{
  const Cursor & cursor = m_doc->activeView()->cursorPosition();
  if ( cursor == *m_finalCursorPosition ) {
    // wrap and jump to first range
    setCurrentRange(m_templateRanges.first());
    return;
  }
  SmartRange* nextRange = 0;
  foreach ( SmartRange* range, m_templateRanges ) {
    if ( range->start() <= cursor ) {
      continue;
    }
    if ( !nextRange || range->start() < nextRange->start() ) {
      nextRange = range;
    }
  }
  if ( nextRange ) {
    setCurrentRange(nextRange);
  } else {
    // wrap and jump to final cursor
    jumpToFinalCursorPosition();
  }
}

void KateTemplateHandler::setCurrentRange(SmartRange* range)
{
  if ( !range->childRanges().isEmpty() ) {
    // jump to first mirrored range
    range = range->childRanges().first();
  }

  if ( m_doc->activeView() ) {
    m_jumping = true;
    if ( m_uneditedRanges.contains(range) ) {
      m_doc->activeView()->setSelection(*range);
    }
    m_doc->activeView()->setCursorPosition(range->start());
    m_jumping = false;
  }

  m_lastCaretPosition = range->start();
}

/**
 * Returns an attribute with \p color as background with 0x88 alpha value.
 */
Attribute::Ptr getAttribute(QColor color)
{
  Attribute::Ptr attribute(new Attribute());
  color.setAlpha(0x88);
  attribute->setBackground(QBrush(color));
  return attribute;
}

void KateTemplateHandler::insertText(const Cursor &position, const QString& text)
{
  ///TODO: maybe use Kate::CutCopyPasteEdit or similar?
  m_doc->editStart();
  if ( m_doc->insertText(position, text) ) {
    m_doc->undoSafePoint();
  }
  m_doc->editEnd();
}

void KateTemplateHandler::handleTemplateString(const Cursor& position, QString templateString,
                                               const QMap< QString, QString >& initialValues)
{
  KateRendererConfig *config = m_doc->activeKateView()->renderer()->config();

  Attribute::Ptr editableAttribute = getAttribute(config->templateEditablePlaceholderColor());
  editableAttribute->setDynamicAttribute(
      Attribute::ActivateCaretIn, getAttribute(config->templateFocusedEditablePlaceholderColor())
  );

  Attribute::Ptr mirroredAttribute = getAttribute(config->templateNotEditablePlaceholderColor());

  int line = position.line();
  int column = position.column();

  // true, when the last char was a backslash
  // and that one wasn't prepended by an odd number
  // of backslashes
  bool isEscaped = false;

  // not equal -1 when we found a start position
  int startPos = -1;

  // each found variable gets it's range(s) added to the list.
  // the key is the varname, e.g. the same as in initialValues
  // to be able to iterate over them in a FIFO matter, also store
  // the keys in a queue.
  QQueue<QString> keyQueue;
  QMultiMap<QString, Range> ranges;

  // valid, if we find an occurrence of ${cursor}
  Cursor finalCursorPosition = Cursor::invalid();

  // parse string for ${VAR} or %{VAR}
  // VAR must not contain $ or %
  // VAR must not contain newlines
  // VAR must be set as key in initialValues
  // expression must not be escaped
  for ( int i = 0; i < templateString.size(); ++i ) {
    if ( templateString[i] == '\\' ) {
      ++column;
      isEscaped = !isEscaped;
    } else if ( templateString[i] == '\n' ) {
      ++line;
      column = 0;
      isEscaped = false;
      if ( startPos != -1 ) {
        // don't allow variables to span multiple lines
        startPos = -1;
      }
    } else if ( templateString[i] == '%' || templateString[i] == '$' ) {
      if ( isEscaped && startPos == -1 ) {
        // remove escape char
        templateString.remove(i - 1, 1);
        --i;
        isEscaped = false;
      } else if ( startPos != -1 ) {
        // don't allow nested variables
        startPos = -1;
        ++column;
      } else if ( i + 2 < templateString.size() && templateString[i + 1] == '{' ) {
        // need at least %{}
        startPos = i;
        // skip {
        ++i;
        column += 2;
      }
    } else if ( templateString[i] == '}' && startPos != -1 ) {
      // get key, i.e. contents between ${..}
      const QString key = templateString.mid( startPos + 2, i - (startPos + 2) );
      if ( !initialValues.contains(key) ) {
        kWarning() << "unknown variable key:" << key;
      } else if ( key == "cursor" ) {
        finalCursorPosition = Cursor(line, column - key.length() - 2);
        // don't insert anything, just remove the placeholder
        templateString.remove(startPos, i - startPos + 1);
        // correct iterator pos, 3 == $ + { + }
        i -= 3 + key.length();
        column -= 2 + key.length();
        startPos = -1;
      } else {
        // whether the variable starts with % or $
        QChar c = templateString[startPos];
        // replace variable with initial value
        templateString.replace( startPos, i - startPos + 1, initialValues[key] );
        // correct iterator pos, 3 == % + { + }
        i -= 3 + key.length() - initialValues[key].length();
        // correct column to point at end of range, taking replacement width diff into account
        // 2 == % + {
        column -= 2 + key.length() - initialValues[key].length();
        // always add ${...} to the editable ranges
        // only add %{...} to the editable ranges when it's value equals the key
        if ( c == '$' || key == initialValues[key] ) {
          if ( !keyQueue.contains(key) ) {
            keyQueue.append(key);
          }
          ranges.insert( key,
                         Range( line, column - initialValues[key].length(),
                                line, column
                              )
                        );
        }
      }
      startPos = -1;
    } else {
      isEscaped = false;
      ++column;
    }
  }

  // need to insert here, so that the following smart range can get their ranges set properly.
  insertText(position, templateString);

  if ( m_doc->activeKateView() ) {
    ///HACK: make sure the view cache is up2date so we can safely set the cursor position
    ///      else one might hit the Q_ASSERT(false) in KateViewInternal::endPos()
    ///      the cause is that the view get's ::update()'d, but that does not trigger a
    ///      paint event right away.
    m_doc->activeKateView()->updateView(true);
  }

  if ( finalCursorPosition.isValid() ) {
    m_finalCursorPosition = m_doc->newSmartCursor(finalCursorPosition);
  } else {
    m_finalCursorPosition = m_doc->newSmartCursor(Cursor(line, column));
  }

  if ( ranges.isEmpty() ) {
    return;
  }

  m_wholeTemplateRange = m_doc->newSmartRange( Range(position, Cursor(line, column)), 0,
                                               SmartRange::ExpandLeft | SmartRange::ExpandRight );
  m_wholeTemplateRange->setAttribute(getAttribute(config->templateBackgroundColor()));
  m_doc->addHighlightToDocument(m_wholeTemplateRange, true);

  // create smart ranges for each found variable
  // if the variable exists more than once, create "mirrored" ranges
  foreach ( const QString& key, keyQueue ) {
    const QList<Range> &values = ranges.values(key);
    // used as parent for mirrored variables,
    // and as only item for not-mirrored variables
    SmartRange* parent = m_doc->newSmartRange(values.first(), 0, SmartRange::ExpandLeft | SmartRange::ExpandRight);
    if ( values.size() > 1 ) {
      // add all "real" ranges as children
      for (int i = 0; i < values.size(); ++i )  {
        SmartRange* range = m_doc->newSmartRange(
          values[i], parent, SmartRange::ExpandLeft | SmartRange::ExpandRight
        );
        // the last item will be our real first range (see multimap docs)
        if ( i == values.size() - 1 ) {
          range->setAttribute(editableAttribute);
          m_uneditedRanges.append(range);
        } else {
          range->setAttribute(mirroredAttribute);
        }
      }
    } else {
      // just a single range
      parent->setAttribute(editableAttribute);
      m_uneditedRanges.append(parent);
    }

    m_doc->addHighlightToDocument(parent, true);
    m_templateRanges.append(parent);
  }

  setCurrentRange(m_templateRanges.first());
}

void KateTemplateHandler::slotTextChanged(Document* document, const Range& range)
{
  Q_ASSERT(document == m_doc);

  kDebug() << "text changed" << document << range;

  if ( m_isMirroring ) {
    kDebug() << "mirroring - ignoring edit";
    return;
  }
  if ( (!m_editWithUndo && m_doc->isEditRunning()) || range.isEmpty() ) {
    kDebug(13020) << "slotTextChanged returning prematurely";
    return;
  }
  if ( m_wholeTemplateRange->isEmpty() ) {
    kDebug() << "template range got deleted, exiting";
    cleanupAndExit();
    return;
  }
  if ( range.start() <= *m_finalCursorPosition && range.end() >= *m_finalCursorPosition ) {
    kDebug() << "editing at final cursor position, exiting.";
    cleanupAndExit();
    return;
  }
  if ( !m_wholeTemplateRange->contains(range) ) {
    // outside our template or one of our own changes
    kDebug() << range << "not contained in" << *m_wholeTemplateRange;
    return;
  }

  kDebug() << "see if we have to mirror the edit";

  // Check if @p range is mirrored.
  // If we have two adjacent mirrored ranges, think ${first}${second},
  // the second will be mirrored, as that's what the user will see "activated",
  // since Kate uses contains() that does a comparison "< end()".
  // We use "<= end()" though so we can handle contents that were added at the end of a range.
  // TODO: make it possible to select either, the left or right rannge (LOWPRIO)

  // The found child range to act as base for mirroring.
  SmartRange* baseRange = 0;
  // The left-adjacent range that gets some special treatment (if it exists).
  SmartRange* leftAdjacentRange = 0;

  foreach ( SmartRange* parent, m_templateRanges ) {
    if ( parent->start() <= range.start() && parent->end() >= range.start() )
    {
      if ( parent->childRanges().isEmpty() ) {
        // simple, not-mirrored range got changed
        m_uneditedRanges.removeOne(parent);
      } else {
        // handle mirrored ranges
        if ( baseRange ) {
          // look for adjacent range (we find the right-handed one here)
          kDebug() << "find adjacent mirror";
          foreach ( SmartRange* child, parent->childRanges() ) {
            kDebug() << "current baserange:" << *baseRange << "vs" << *child;
            if ( child->start() == range.start() && child->end() >= range.end() ) {
              kDebug() << "found adjacent";
              leftAdjacentRange = baseRange;
              baseRange = child;
              break;
            }
          }
          // adjacent mirror handled, we can finish
          break;
        } else {
          // find mirrored range that got edited
          foreach ( SmartRange* child, parent->childRanges() ) {
            if ( child->start() <= range.start() && child->end() >= range.start() ) {
              baseRange = child;
              break;
            }
          }
          if ( baseRange && baseRange->end() != range.end() ) {
            // finish, don't look for adjacent mirror as we are not at the end of this range
            break;
          } // else don't finish, look for baseRange or adjacent mirror first
        }
      }
    } else if ( baseRange ) {
      // no adjacent mirrored range found, we can finish
      break;
    }
  }
  if ( baseRange ) {
    m_uneditedRanges.removeOne(baseRange);
    if ( leftAdjacentRange ) {
      // revert that something got added to the adjacent range
      kDebug() << "removing edited range" << range << "from left adjacent range" << *leftAdjacentRange;
      leftAdjacentRange->end() = range.start();
      kDebug() << "new range:" << *leftAdjacentRange;
    }
    syncMirroredRanges(baseRange);
    if ( range.start() == baseRange->start() && m_doc->activeKateView() ) {
      // TODO: update the attribute, since kate doesn't do that automatically
      // TODO: fix in kate itself
      // TODO: attribute->changed == undefined reference...
    }
  } else {
    kDebug() << "no range found to mirror this edit";
  }
}

void KateTemplateHandler::syncMirroredRanges(SmartRange* range)
{
  kDebug() << "mirroring contents from" << *range;
  Q_ASSERT(m_templateRanges.contains(range->parentRange()));

  m_isMirroring = true;
  m_doc->editStart();

  const QStringList &newText = m_doc->textLines(*range);

  foreach ( SmartRange* sibling, range->parentRange()->childRanges() ) {
    if ( sibling != range ) {
      sibling->replaceText(newText);
    }
  }

  /// TODO: now undo only undos the last char edit, and does not
  ///       merge those edits as usual
  bool undoDontMerge = m_doc->undoDontMerge();
  m_doc->setUndoDontMerge(false);
  m_doc->setUndoAllowComplexMerge(true);
  m_doc->undoSafePoint();
  m_doc->editEnd();
  m_doc->setUndoDontMerge(undoDontMerge);
  m_isMirroring = false;
}

#include "katetemplatehandler.moc"

// kate: space-indent on; indent-width 2; replace-tabs on;
