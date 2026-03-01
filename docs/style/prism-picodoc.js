Prism.languages.picodoc = {
    'comment':    { pattern: /#\/\/.*/ },
    'string':     [
        { pattern: /"{3,}[\s\S]*?"{3,}/, greedy: true },
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
