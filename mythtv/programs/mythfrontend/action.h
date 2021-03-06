/* -*- myth -*- */
/**
 * @file action.h
 * @author Micah F. Galizia <mfgalizi@csd.uwo.ca>
 * @brief Main header for the action class.
 *
 * Copyright (C) 2005 Micah Galizia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */
#ifndef ACTION_H
#define ACTION_H

#include <utility>

// Qt headers
#include <QHash>
#include <QStringList>

/** \class Action
 *  \brief An action (for this plugin) consists of a description,
 *         and a set of key sequences.
 *
 *   On its own, the action cannot actually identify a particular
 *   action. This is a class to make the keybinding class easier
 *   to manage.
 */
class Action
{
  public:
    /// \brief Create a new empty action.
    explicit Action(QString description) : m_description(std::move(description)) {}
    Action(QString description, const QString &keys);

    // Commands
    bool AddKey(const QString &key);
    bool ReplaceKey(const QString &newkey, const QString &oldkey);
    /// \brief Remove a key from this action.
    /// \return true on success, false otherwise.
    bool RemoveKey(const QString &key)
    {
        return m_keys.removeAll(key) != 0;
    }

    // Gets
    /// \brief Returns the action description. (note: not threadsafe)
    QString     GetDescription(void) const { return m_description; }
    /// \brief Returns the key sequence(s) that trigger this action.
    ///        (note: not threadsafe)
    QStringList GetKeys(void)        const { return m_keys; }
    /// \brief Returns comma delimited string of key bindings
    QString     GetKeyString(void)   const { return m_keys.join(","); }
    /// \brief Returns true iff the action has no keys
    bool        IsEmpty(void)        const { return m_keys.empty(); }
    bool        HasKey(const QString &key) const;

  public:
    /// \brief The maximum number of keys that can be bound to an action.
    static const unsigned int kMaximumNumberOfBindings = 4;

  private:
    QString     m_description; ///< The actions description.
    QStringList m_keys;        ///< The keys bound to the action.
};
using Context = QHash<QString, Action*>;

/** \class ActionID
 *  \brief A class that uniquely identifies an action.
 *
 *  Actions are identified based on their action name and context.
 */
class ActionID
{
  public:
    /// \brief Create an empty action
    ActionID(void);

    /** \brief Create a new action identifier
     *  \param context The action's context
     *  \param action The action's name
     */
    ActionID(QString context, QString action)
        : m_context(std::move(context)), m_action(std::move(action)) {}
    ActionID(const ActionID&) = default;
    ActionID& operator=(const ActionID&) = default;

    /// \brief Returns the context name.
    QString GetContext(void) const { return m_context; }

    /// \brief Returns the action name.
    QString GetAction(void)  const { return m_action; }

    bool operator==(const ActionID &other) const
    {
        return ((m_action  == other.m_action) &&
                (m_context == other.m_context));
    }

  private:
    QString m_context;
    QString m_action;
};
using ActionList = QList<ActionID>;

#endif /* ACTION_H */
