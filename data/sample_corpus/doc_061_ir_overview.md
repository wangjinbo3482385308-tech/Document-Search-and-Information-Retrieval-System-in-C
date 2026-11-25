# Information Retrieval Pipeline Overview

A classical document search engine executes document loading, tokenization, text normalization, inverted index construction, query processing, deterministic relevance scoring (Basic TF, TF-IDF, and Okapi BM25), and ranked presentation.

Positional posting lists allow exact phrase search such as "renewable energy" and "hash table" without re-scanning raw document files on every query.
