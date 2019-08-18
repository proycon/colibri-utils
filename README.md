Colibri Utils
===================

This collection of Natural Language Processing utilities currently contains only a single tool:

* **Colibri Lang: Language identification** - ``colibri-lang`` - Detects in what language parts of a document are. Works on both [FoLiA XML](https://proycon.github.io/folia) documents as well as plain text. When given FoLiA XML input, the document is enriched with [language annotation](https://folia.readthedocs.io/en/folia2.0/lang_annotation.html) and may be applied to any structural level FoLiA supports (e.g. paragraphs, sentences, etc..). When given plain text input, each input line is classified. This tool currently supports a limited subset of languages, but is easily extendable:
    * English, Spanish, Dutch, French, Portuguese, German, Italian, Swedish, Danish (trained on Europarl)
    * Latin (trained on the Clementine Vulgate bible, and some on a few latin works from Project Gutenberg)
    * Historical Dutch:
        * Middle dutch - Trained on Corpus van Reenen/Mulder and Corpus Gysseling
        * Early new dutch  - Trained on Brieven als Buit

Installation
-----------------

Colibri Utils is included in our [LaMachine distribution](https://proycon.github.io/LaMachine), which is the easiest and recommended way of obtaining it.

Colibri Utils is written in C++. Building from source is also possible if you
have the expertise, but requires various dependencies, including
[ticcutils](https://github.com/LanguageMachines/ticcutils), [libfolia](https://github.com/LanguageMachines/libfolia), and
[colibri-core](https://github.com/proycon/colibri-core)

Usage
---------

See ``colibri-lang --help``

Methodology
-------------

To identify languages, input tokens are matches against a trained lexicon with
token frequencies, which are loaded in memory. No higher order n-grams are used.

A pseudo-probability is computed for the given sequence of input tokens for
each language, the highest probability wins. A confidence value is computed
simply as the ratio of tokens in the vocabulary divided by the length of the
token sequence. Out of vocabulary words are assigned a very low probability.

Training
----------

New models can easily be trained and added, and are independent of the other
models. Simply train an unindexed patternmodel with [Colibri
Core](https://proycon.github.io/colibri-core) and put the model file and the
class file in your data directory. Ensure the data is tokenised and lower-cased
prior to building a pattern model
([ucto](https://github.com/LanguageMachines/ucto) can do both of this for you).



