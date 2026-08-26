"
A service that writes: what the live test uses to see a request's transaction
committed on success and rolled back on failure.
"
Class {
	#name : 'Ledger',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Ledger >> reset [
	(db tableExists: 'ledger') ifTrue: [db executeImmediate: 'DROP TABLE ledger'].
	db executeImmediate: 'CREATE TABLE ledger (id INTEGER, amount INTEGER)'.
	outjson at: 'ok' put: true
]

{ #category : 'services' }
Ledger >> deposit [
	db execute: 'INSERT INTO ledger VALUES (?, ?)'
		with: (Array with: (injson integerAt: 'id') with: (injson integerAt: 'amount')).
	outjson at: 'ok' put: true
]

{ #category : 'services' }
Ledger >> depositThenFail [
	"The row goes in, and then the request fails: the row must not stay."
	db execute: 'INSERT INTO ledger VALUES (?, ?)'
		with: (Array with: (injson integerAt: 'id') with: (injson integerAt: 'amount')).
	RestUserError code: 9 signal: 'changed my mind'
]

{ #category : 'services' }
Ledger >> count [
	outjson at: 'count' put: ((db fetchOne: 'SELECT COUNT(*) AS n FROM ledger') integerAt: 'n')
]
