"
The CRUD screen's service: a phone list in the table `phone', four methods
the screen calls, and no build step -- this file is read into the running
server the first time a request names services.Crud, and read again when
it changes on disk.  Edit it while the server runs and the next call runs
the new code.

Every method here runs after the dispatcher has checked the login, so all
of them are authenticated, and inside a transaction on db that is
committed when the method returns and rolled back if it raises.

Two of Kiss's methods are not here: runReport, which makes a PDF through
groff, and runExport, which writes a CSV, both served back as files.  This
server has no way to serve a file it made and no groff; the screen's
Report and Export buttons answer `No such method' until that is built.
"
Class {
	#name : 'Crud',
	#superclass : 'RestService',
	#category : 'Web-Demo'
}

{ #category : 'services' }
Crud >> getRecords [
	"Every row, ordered by name -- or `nodb', which the screen reads as
	 `this needs a database'."
	| rows row |
	db isNil ifTrue: [outjson at: 'nodb' put: true. ^self].
	rows := JSONArray new.
	"row is a method temporary and not a block one: a service file is compiled
	 by the image's own compiler, 1983's, which has no block temporaries."
	(db fetchAll: 'select * from phone order by last_name, first_name') do: [:rec |
		row := JSONObject new.
		row at: 'id' put: (rec integerAt: 'rec_id');
			at: 'firstName' put: (rec stringAt: 'first_name');
			at: 'lastName' put: (rec stringAt: 'last_name');
			at: 'phoneNumber' put: (rec stringAt: 'phone_number').
		rows add: row].
	outjson at: 'rows' put: rows
]

{ #category : 'services' }
Crud >> addRecord [
	| rec |
	rec := db newRecord: 'phone'.
	rec at: 'first_name' put: (injson stringAt: 'firstName');
		at: 'last_name' put: (injson stringAt: 'lastName');
		at: 'phone_number' put: (injson stringAt: 'phoneNumber').
	rec insert
]

{ #category : 'services' }
Crud >> updateRecord [
	| rec |
	rec := db fetchOne: 'select * from phone where rec_id = ?' with: (Array with: (injson integerAt: 'id')).
	rec isNil ifTrue: [^RestUserError code: 1 signal: 'no such record'].
	rec at: 'first_name' put: (injson stringAt: 'firstName');
		at: 'last_name' put: (injson stringAt: 'lastName');
		at: 'phone_number' put: (injson stringAt: 'phoneNumber').
	rec update
]

{ #category : 'services' }
Crud >> deleteRecord [
	db execute: 'delete from phone where rec_id = ?' with: (Array with: (injson integerAt: 'id'))
]
