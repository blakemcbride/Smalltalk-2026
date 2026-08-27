"
The Users screen's service: the accounts that can log in, in the table
`users'.  Kiss's Users.groovy.

The password column never leaves the server: getRecords does not select
it, and the screen sends a blank password on an edit to mean `leave it'.
A new or changed password is stored as PasswordHash hash: writes it --
PBKDF2, salted, six hundred thousand rounds -- which is what Login checks
against.  A user set inactive is logged out within two minutes, when
Login>>checkLogin:request: is next asked about them.
"
Class {
	#name : 'Users',
	#superclass : 'RestService',
	#category : 'Web-Demo'
}

{ #category : 'services' }
Users >> getRecords [
	| rows row |
	db isNil ifTrue: [outjson at: 'nodb' put: true. ^self].
	rows := JSONArray new.
	(db fetchAll: 'select user_id, user_name, user_active from users order by user_name') do: [:rec |
		row := JSONObject new.
		row at: 'id' put: (rec integerAt: 'user_id');
			at: 'userName' put: (rec stringAt: 'user_name');
			at: 'userActive' put: (rec stringAt: 'user_active').
		rows add: row].
	outjson at: 'rows' put: rows
]

{ #category : 'services' }
Users >> addRecord [
	| rec |
	rec := db newRecord: 'users'.
	rec at: 'user_name' put: (injson stringAt: 'userName');
		at: 'user_password' put: (PasswordHash hash: (injson stringAt: 'userPassword'));
		at: 'user_active' put: (injson stringAt: 'userActive').
	rec insert
]

{ #category : 'services' }
Users >> updateRecord [
	| rec password |
	rec := db fetchOne: 'select * from users where user_id = ?' with: (Array with: (injson integerAt: 'id')).
	rec isNil ifTrue: [^RestUserError code: 1 signal: 'no such user'].
	rec at: 'user_name' put: (injson stringAt: 'userName').
	"Only when a new one was typed: the client never has the old one to send back."
	password := injson stringAt: 'userPassword'.
	(password notNil and: [password isEmpty not])
		ifTrue: [rec at: 'user_password' put: (PasswordHash hash: password)].
	rec at: 'user_active' put: (injson stringAt: 'userActive').
	rec update
]

{ #category : 'services' }
Users >> deleteRecord [
	db execute: 'delete from users where user_id = ?' with: (Array with: (injson integerAt: 'id'))
]
