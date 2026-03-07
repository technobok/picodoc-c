Prism.languages.picodoc = {
    'comment': [
        { pattern: /\[#(?:comment\b|\/\/)[\s\S]*?\]/, greedy: true },
        { pattern: /#(?:comment\b|\/\/)\s*:\s*\n(?:[ \t]*\S[^\n]*(?:\n|$))*/, greedy: true },
        { pattern: /#(?:comment\b|\/\/).*/ }
    ],
    'string':     [
        { pattern: /"{6}[\s\S]*?"{6}/, greedy: true },
        { pattern: /"{5}[\s\S]*?"{5}/, greedy: true },
        { pattern: /"{4}[\s\S]*?"{4}/, greedy: true },
        { pattern: /"{3}[\s\S]*?"{3}/, greedy: true },
        { pattern: /"(?:[^"\\]|\\.)*"/, greedy: true }
    ],
    'escape':     /\\(?:[#\[\]\\"]|x[0-9a-fA-F]{2}|U[0-9a-fA-F]{8}|[nt])/,
    'keyword':    [
        { pattern: /\[#[\w.*!@~-]+/, inside: {
            'punctuation': /^\[/,
            'keyword':     /#[\w.*!@~-]+/
        }},
        { pattern: /#[\w.*!@~-]+/ }
    ],
    'attr-name':  { pattern: /\b[\w.]+(?==)/ },
    'punctuation': /[:\[\]]/
};
