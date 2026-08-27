"
The LLM classes against the real services, with real keys in the
environment: ANTHROPIC_API_KEY, OPENAI_API_KEY, OPENROUTER_API_KEY, and an
Ollama on localhost.  Each test fails, naming its key, when the key is not
there; the profile is run on purpose, and a suite that passed while asking
nobody would teach its reader to ignore it.

    ANTHROPIC_API_KEY=... ./st80 -bootstrap -profile profiles/llm-live.profile -tests

Not in tests/profiles.expected, for the reason database-live is not.
"
Profile {
	#name     : 'llm-live',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#packages : [ '../lib/LLM-Live-Tests' ]
}
