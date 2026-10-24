using System.Collections.Generic;

namespace TmDq.Core.Tests;

/// <summary>
/// Inputs and the core's own answers for the file formats: pattern files, CSV files, pattern writes, template
/// matches, clock stamps and CSV writes. Recorded by running the core's functions (readFrameSyncFile,
/// writeFrameSyncFile, CsvSeriesParser::parse, CsvSeriesWriter::write, TemplateMatcher::match, clocktime::format)
/// directly over exactly these inputs, with a throwaway diagnostic. Doubles are printed with 17 significant digits,
/// so they parse back to the exact value; text is hex; "-" is an empty string or list.
/// Result lines: P|name|pattern|mask|hasBits|bits|legacy|inverted|hasRandomized|randomized;
/// C|name|success|baseDay|baseOffset|xMax|skipped|n, then n of s|i|metric|name|label|receiver|channel|order|source|yMin|yMax|xs|ys;
/// W|name|ok|bytes; M|name|ok|missing|extra; K|name|text; X|name|ok|bytes.
/// </summary>
internal static class FormatsDigest
{
    /// <summary>Pattern files by name, base64.</summary>
    public static readonly Dictionary<string, string> PatternFiles = new()
    {
        ["another-section"] = "W090aGVyXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiCkJpdHNQZXJGcmFtZSA9IDgwMAo=",
        ["bits-and-words-too-small"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAzMgpXb3Jkc0luTWlub3JGcmFtZSA9IDMK",
        ["bits-as-text"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAiODAwIgo=",
        ["bits-at-minimum"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA2NAo=",
        ["bits-below-minimum"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA2Mwo=",
        ["bits-fraction"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA4MDAuNQo=",
        ["bits-large"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA3MDAwMAo=",
        ["bits-minimum-beats-words"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA2NApXb3Jkc0luTWlub3JGcmFtZSA9IDEwMAo=",
        ["bits-negative"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAtODAwCg==",
        ["bits-padded"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAgIDgwMCAgIAo=",
        ["bits-plus"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSArODAwCg==",
        ["bits-too-small-then-words"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAzMgpXb3Jkc0luTWlub3JGcmFtZSA9IDY0Cg==",
        ["bits-zero"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSAwCg==",
        ["both-flags-off"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiCkZyYW1lU3luY01hc2sgPSAiRkZGRjAwMDAiCkJpdHNQZXJGcmFtZSA9IDgwMApJbnZlcnRlZCA9IGZhbHNlClJhbmRvbWl6ZWQgPSBmYWxzZQo=",
        ["complete"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiCkZyYW1lU3luY01hc2sgPSAiRkZGRjAwMDAiCkJpdHNQZXJGcmFtZSA9IDgwMApJbnZlcnRlZCA9IHRydWUKUmFuZG9taXplZCA9IHRydWUK",
        ["crlf"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiDQpCaXRzUGVyRnJhbWUgPSA4MDANClJhbmRvbWl6ZWQgPSB0cnVlDQo=",
        ["duplicate-keys-last-wins"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiQUFBQSIKRnJhbWVTeW5jID0gIkJCQkIiCg==",
        ["empty"] = "",
        ["empty-values"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiIgpGcmFtZVN5bmNNYXNrID0gIiIK",
        ["inverted-empty"] = "W0ZyYW1lXQpJbnZlcnRlZCA9Cg==",
        ["inverted-false"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IGZhbHNlCg==",
        ["inverted-one"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IDEK",
        ["inverted-quoted"] = "W0ZyYW1lXQpJbnZlcnRlZCA9ICJ0cnVlIgo=",
        ["inverted-true"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IHRydWUK",
        ["inverted-uppercase"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IFRSVUUK",
        ["inverted-yes"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IHllcwo=",
        ["inverted-zero"] = "W0ZyYW1lXQpJbnZlcnRlZCA9IDAK",
        ["key-case-matters"] = "W0ZyYW1lXQpmcmFtZXN5bmMgPSAiRkU2QjI4NDAiCmJpdHNwZXJmcmFtZSA9IDgwMAo=",
        ["lowercase-kept-as-written"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiZmU2YjI4NDAiCkZyYW1lU3luY01hc2sgPSAiZmZmZjAwMDAiCg==",
        ["mask-only"] = "W0ZyYW1lXQpGcmFtZVN5bmNNYXNrID0gIkZGMDBGRjAwIgo=",
        ["no-section"] = "RnJhbWVTeW5jID0gIkZFNkIyODQwIgpCaXRzUGVyRnJhbWUgPSA4MDAK",
        ["non-ascii-pattern"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAic3RyYcOfZSDOuyIK",
        ["not-hex-kept"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAienotMSIKRnJhbWVTeW5jTWFzayA9ICJ4Igo=",
        ["padded-value"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiICBBQiAgIgo=",
        ["pattern-only"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiMzM0QUFCQkYiCg==",
        ["randomized-absent"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiQUIiCkludmVydGVkID0gdHJ1ZQo=",
        ["randomized-empty"] = "W0ZyYW1lXQpSYW5kb21pemVkID0K",
        ["randomized-false"] = "W0ZyYW1lXQpSYW5kb21pemVkID0gZmFsc2UK",
        ["randomized-one"] = "W0ZyYW1lXQpSYW5kb21pemVkID0gMQo=",
        ["randomized-quoted-false"] = "W0ZyYW1lXQpSYW5kb21pemVkID0gImZhbHNlIgo=",
        ["randomized-true"] = "W0ZyYW1lXQpSYW5kb21pemVkID0gdHJ1ZQo=",
        ["randomized-yes"] = "W0ZyYW1lXQpSYW5kb21pemVkID0geWVzCg==",
        ["randomized-zero"] = "W0ZyYW1lXQpSYW5kb21pemVkID0gMAo=",
        ["shipped-framesync_PRN11"] = "W0ZyYW1lXQ0KQml0c1BlckZyYW1lID0gMjA0Nw0KRnJhbWVTeW5jID0gIkEzNDVDQTVDIg0KRnJhbWVTeW5jTWFzayA9ICJGRkZGRkZGRiINClJhbmRvbWl6ZWQgPSBmYWxzZQ0K",
        ["shipped-framesync_PRN15"] = "W0ZyYW1lXQ0KQml0c1BlckZyYW1lID0gMzI3NjcNCkZyYW1lU3luYyA9ICIzMzRBQUJCRiINCkZyYW1lU3luY01hc2sgPSAiRkZGRkZGRkYiDQpSYW5kb21pemVkID0gZmFsc2UNCg==",
        ["shipped-framesync_rcvr_default"] = "IyBEZWZhdWx0IFJlY2VpdmVyIFNOUiBmcmFtZSBzeW5jIHBhcmFtZXRlcnMuDQojDQojIFRoZSBhcHBsaWNhdGlvbiBsb2FkcyB0aGlzIGZpbGUgYXMgdGhlIGRlZmF1bHQgZnJhbWUgc3luYyBmb3IgUmVjZWl2ZXIgU05SDQojIHN0cmVhbXMsIHNvIHRoZXNlIHZhbHVlcyBjYW4gYmUgY2hhbmdlZCB3aXRob3V0IHJlYnVpbGRpbmcuIEVkaXQgdGhlbSB0byBtYXRjaA0KIyB5b3VyIHJlY2VpdmVyIHRlbGVtZXRyeSBmb3JtYXQuDQojDQojICAgRnJhbWVTeW5jICAgICAtIGZyYW1lIHN5bmMgcGF0dGVybiAoaGV4KQ0KIyAgIEZyYW1lU3luY01hc2sgLSBmcmFtZSBzeW5jIG1hc2sgKGhleCk7IEZGRkZGRkZGID0gYWxsIDMyIGJpdHMgc2lnbmlmaWNhbnQNCiMgICBCaXRzUGVyRnJhbWUgIC0gdG90YWwgYml0cyBwZXIgbWlub3IgZnJhbWUgKGluY2x1ZGluZyB0aGUgc3luYyB3b3JkcykNCiMgICBSYW5kb21pemVkICAgIC0gdHJ1ZSBpZiB0aGUgc3RyZWFtIGlzIFJOUlotTCBhbmQgbXVzdCBiZSBkZXJhbmRvbWl6ZWQuDQojICAgICAgICAgICAgICAgICAgIFByZXNlbnQgYW5kIGZhbHNlIGhlcmUgb24gcHVycG9zZTogbG9hZGluZyB0aGlzIGZpbGUgdGhlbg0KIyAgICAgICAgICAgICAgICAgICBTRVRTIHRoZSBEZXJhbmRvbWl6ZSBib3ggcmF0aGVyIHRoYW4gbGVhdmluZyB3aGF0ZXZlciB3YXMNCiMgICAgICAgICAgICAgICAgICAgYWxyZWFkeSB0aWNrZWQsIHNvIHRoZSBsb2FkZWQgc3RhdGUgaXMgYWx3YXlzIGV4cGxpY2l0Lg0KDQpbRnJhbWVdDQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiDQpGcmFtZVN5bmNNYXNrID0gIkZGRkZGRkZGIg0KQml0c1BlckZyYW1lID0gODAwDQpSYW5kb21pemVkID0gZmFsc2UNCg==",
        ["trailing-comment"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiICMgdGhlIHBhdHRlcm4KQml0c1BlckZyYW1lID0gODAwICMgYml0cwo=",
        ["unquoted-value"] = "W0ZyYW1lXQpGcmFtZVN5bmMgPSBGRTZCMjg0MAo=",
        ["utf16be-bom"] = "/v8AWwBGAHIAYQBtAGUAXQAKAEYAcgBhAG0AZQBTAHkAbgBjACAAPQAgACIARgBFADYAQgAyADgANAAwACIACgBCAGkAdABzAFAAZQByAEYAcgBhAG0AZQAgAD0AIAA4ADAAMAAK",
        ["utf16le-bom"] = "//5bAEYAcgBhAG0AZQBdAAoARgByAGEAbQBlAFMAeQBuAGMAIAA9ACAAIgBGAEUANgBCADIAOAA0ADAAIgAKAEIAaQB0AHMAUABlAHIARgByAGEAbQBlACAAPQAgADgAMAAwAAoAUgBhAG4AZABvAG0AaQB6AGUAZAAgAD0AIAB0AHIAdQBlAAoA",
        ["utf8-bom"] = "77u/W0ZyYW1lXQpGcmFtZVN5bmMgPSAiRkU2QjI4NDAiCkJpdHNQZXJGcmFtZSA9IDgwMAo=",
        ["valid-bits-beat-words"] = "W0ZyYW1lXQpCaXRzUGVyRnJhbWUgPSA4MDAKV29yZHNJbk1pbm9yRnJhbWUgPSA2NAo=",
        ["words-1"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IDEK",
        ["words-2"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IDIK",
        ["words-4"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IDQK",
        ["words-64"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IDY0Cg==",
        ["words-huge"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IDEwMDAwMAo=",
        ["words-negative"] = "W0ZyYW1lXQpXb3Jkc0luTWlub3JGcmFtZSA9IC02NAo=",
    };

    /// <summary>CSV files by name, base64.</summary>
    public static readonly Dictionary<string, string> CsvFiles = new()
    {
        ["all-three-kinds"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxBIEFjY3VtdWxhdGVkIE1pc3NlZCBGcmFtZXMsNDAgLSBDaCBBIExfUkNWUjIKMTowMDowMDowMC4wMDAsOTAsMCwtODAuNQoxOjAwOjAwOjAxLjAwMCw5MSwxLC03OS41Cg==",
        ["basic-lock"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCw1MAoxOjAwOjAwOjAxLjAwMCw2MAo=",
        ["blank-first-line"] = "ClRpbWUgKERPWTpISDpNTTpTUy5tbW0pLEEgTG9jayAoJSkKMTowMDowMDowMC4wMDAsNTAKMTowMDowMDowMS4wMDAsNjAK",
        ["blank-lines"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoKMTowMDowMDowMC4wMDAsNTAKCgoxOjAwOjAwOjAxLjAwMCw2MAoK",
        ["cell-number-forms"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCwxZTIKMTowMDowMDowMS4wMDAsLjUKMTowMDowMDowMi4wMDAsNS4KMTowMDowMDowMy4wMDAsMUUtMQoxOjAwOjAwOjA0LjAwMCwweDEwCjE6MDA6MDA6MDUuMDAwLDFfMAo=",
        ["cell-overflow"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCwxZTk5OQoxOjAwOjAwOjAxLjAwMCwxZS05OTkKMTowMDowMDowMi4wMDAsNC45ZS0zMjQK",
        ["cell-padding"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCwgNSAKMTowMDowMDowMS4wMDAsKzYKMTowMDowMDowMi4wMDAsLTcKMTowMDowMDowMy4wMDAsCTgJCg==",
        ["cell-special-values"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCxuYW4KMTowMDowMDowMS4wMDAsaW5mCjE6MDA6MDA6MDIuMDAwLC1pbmYKMTowMDowMDowMy4wMDAsTmFOCjE6MDA6MDA6MDQuMDAwLEluZmluaXR5CjE6MDA6MDA6MDUuMDAwLCtpbmYK",
        ["channel-index-counts"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksNDAgLSBBIExfUkNWUjEsNDAgLSBBIFJfUkNWUjEsNDAgLSBBIENfUkNWUjEsNDAgLSBBIExfUkNWUjIsNDEgLSBCIExfUkNWUjEsNDEgLSBCIFJfUkNWUjEKMTowMDowMDowMC4wMDAsMSwyLDMsNCw1LDYK",
        ["crlf"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQ0KMTowMDowMDowMC4wMDAsNTANCjE6MDA6MDA6MDEuMDAwLDYwDQo=",
        ["duplicate-headers"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxBIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLDEsMgo=",
        ["empty-cell"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxCIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLDEsCjE6MDA6MDA6MDEuMDAwLCw0CjE6MDA6MDA6MDIuMDAwLDUsNgo=",
        ["empty-file"] = "",
        ["empty-series-header"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksLEEgTG9jayAoJSkKMTowMDowMDowMC4wMDAsMSwyCg==",
        ["every-cell-bad"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCx4CjE6MDA6MDA6MDEuMDAwLHkK",
        ["every-row-bad"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQpub3QgYSB0aW1lLDEKYWxzbyBub3QsMgo=",
        ["extra-fields"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCwxLDIsMwoxOjAwOjAwOjAxLjAwMCw0LDUK",
        ["first-good-row-sets-base"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQpiYWQsMQoyOjAzOjA0OjA1LjI1MCwyCjI6MDM6MDQ6MDYuMjUwLDMK",
        ["first-row-bad-value"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoyOjAzOjA0OjA1LjI1MCx4CjI6MDM6MDQ6MDYuMjUwLDMK",
        ["first-row-short"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxCIExvY2sgKCUpCjI6MDM6MDQ6MDUuMjUwLDEKMjowMzowNDowNi4yNTAsMiwzCg==",
        ["header-case"] = "dGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCw1MAo=",
        ["header-only"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQo=",
        ["invalid-utf8-name"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksUuljZXB0ZXVyIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLDUwCg==",
        ["lock-orders-after-snr"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksWmVkIExvY2sgKCUpLDQwIC0gQSBMX1JDVlIxLEEgTG9jayAoJSksWmVkIEFjY3VtdWxhdGVkIE1pc3NlZCBGcmFtZXMKMTowMDowMDowMC4wMDAsMSwyLDMsNAo=",
        ["lone-cr"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQ0xOjAwOjAwOjAwLjAwMCw1MA0xOjAwOjAwOjAxLjAwMCw2MA0=",
        ["mixed-line-ends"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQ0KMTowMDowMDowMC4wMDAsNTAKMTowMDowMDowMS4wMDAsNjANCg==",
        ["no-quoting"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksIkEsQiBMb2NrICglKSIKMTowMDowMDowMC4wMDAsIjUiLDYK",
        ["no-trailing-newline"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCw1MAoxOjAwOjAwOjAxLjAwMCw2MA==",
        ["non-ascii-names"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksUsOpY2VwdG9yIM6pIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLDUwCg==",
        ["series-header-padded"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksICBBIExvY2sgKCUpIAoxOjAwOjAwOjAwLjAwMCw1MAo=",
        ["short-row"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxCIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLDEsMgoxOjAwOjAwOjAxLjAwMCwzCjE6MDA6MDA6MDIuMDAwLDUsNgo=",
        ["sources-keep-counts"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksNDAgLSBBIExfUkNWUjEsNDAgLSBBIFJfUkNWUjEsUzF8IDQwIC0gQSBMX1JDVlIxLFMxfCA0MCAtIEEgUl9SQ1ZSMQoxOjAwOjAwOjAwLjAwMCwxLDIsMyw0Cg==",
        ["time-backwards"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjA1LDEKMTowMDowMDowMSwyCjE6MDA6MDA6MDksMwo=",
        ["time-column-alone"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSkKMTowMDowMDowMC4wMDAK",
        ["time-empty-field"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjowMDowMCwxCjE6MDA6MDA6MDEsMgo=",
        ["time-exponent"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjFlMSwxCjE6MDA6MDA6MDEsMgo=",
        ["time-five-fields"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwOjAwLDEKMTowMDowMDowMSwyCg==",
        ["time-fraction-day"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxLjU6MDA6MDA6MDAsMQoxOjAwOjAwOjAxLDIK",
        ["time-header-padded"] = "ICBUaW1lIChET1k6SEg6TU06U1MubW1tKSAJLEEgTG9jayAoJSkKMTowMDowMDowMC4wMDAsNTAK",
        ["time-midnight"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjIzOjU5OjU5LjUwMCwxCjI6MDA6MDA6MDAuMjUwLDIK",
        ["time-negative-day"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQotMTowMDowMDowMCwxCjA6MDA6MDA6MDAsMgo=",
        ["time-out-of-range"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjI1OjYxOjc1LjUsMQoxOjAwOjAwOjAwLDIK",
        ["time-repeated"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAxLDEKMTowMDowMDowMSwyCg==",
        ["time-signed"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQorMTorMDorMDorMCwxCjE6MDA6MDA6MDEsMgo=",
        ["time-spaced"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQogMSA6IDAgOiAwIDogMCAsMQoxOjAwOjAwOiAxLjUsMgo=",
        ["time-three-fields"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwLDEKMTowMDowMDowMSwyCg==",
        ["time-unpadded"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjA6MDowLDEKMTowOjA6MS41LDIK",
        ["time-wide"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQowMDE6MDA6MDA6MDAuMDAwLDEKMDAwMTowMDowMDowMS4wMDAsMgo=",
        ["trailing-comma-header"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSwKMTowMDowMDowMC4wMDAsNTAsNwo=",
        ["two-sources-one-label"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxTMnwgQSBMb2NrICglKSxTMnwgQSBBY2N1bXVsYXRlZCBNaXNzZWQgRnJhbWVzCjE6MDA6MDA6MDAuMDAwLDEsMiwzCg==",
        ["unparseable-cell"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKSxCIExvY2sgKCUpCjE6MDA6MDA6MDAuMDAwLHgsMgoxOjAwOjAwOjAxLjAwMCwzLDQK",
        ["utf16be-bom"] = "/v8AVABpAG0AZQAgACgARABPAFkAOgBIAEgAOgBNAE0AOgBTAFMALgBtAG0AbQApACwAQQAgAEwAbwBjAGsAIAAoACUAKQAKADEAOgAwADAAOgAwADAAOgAwADAALgAwADAAMAAsADUAMAAKADEAOgAwADAAOgAwADAAOgAwADEALgAwADAAMAAsADYAMAAK",
        ["utf16le-bom"] = "//5UAGkAbQBlACAAKABEAE8AWQA6AEgASAA6AE0ATQA6AFMAUwAuAG0AbQBtACkALABBACAATABvAGMAawAgACgAJQApAAoAMQA6ADAAMAA6ADAAMAA6ADAAMAAuADAAMAAwACwANQAwAAoAMQA6ADAAMAA6ADAAMAA6ADAAMQAuADAAMAAwACwANgAwAAoA",
        ["utf8-bom"] = "77u/VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCw1MAoxOjAwOjAwOjAxLjAwMCw2MAo=",
        ["whitespace-line"] = "VGltZSAoRE9ZOkhIOk1NOlNTLm1tbSksQSBMb2NrICglKQoxOjAwOjAwOjAwLjAwMCw1MAogICAKMTowMDowMDowMS4wMDAsNjAK",
        ["wrong-time-header"] = "RGF5LFRpbWUsQSBMb2NrICglKQoxLDAwOjAwOjAwLDUwCg==",
    };

    /// <summary>The operations: W name pattern mask bits inverted randomized; M name template file; K name day offset elapsed ms; X name day offset columns, each followed by its col lines.</summary>
    public static readonly string[] Ops =
    {
        "W typical 4645364232383430 4646464630303030 800 1 1",
        "W flags-off 3333344141424246 - 32767 0 0",
        "W empty-all - - 0 0 0",
        "W quotes-and-backslash 4122425c43 22 64 0 1",
        "W non-ascii 636166c3a920cea9 c3a9 1024 1 0",
        "W embedded-newline 41420a4344 - 64 0 0",
        "W negative-bits 4142 - -5 0 0",
        "W maximum 46464646464646464646464646464646 46464646464646464646464646464646 65536 1 1",
        "M same 13,14,15 13,14,15",
        "M reordered 13,14,15 15,13,14",
        "M missing-one 13,14,15 13,15",
        "M extra-one 13,15 13,14,15",
        "M missing-and-extra 13,14 14,15,16",
        "M repeats 13,13,14 14,14,13",
        "M both-empty - -",
        "M empty-template - 5,3,5",
        "M empty-file 7,2,7 -",
        "M unsorted-output 9,3,7,1 8,2,6,4",
        "M negatives-and-extremes -1,2147483647,-2147483648,0 0,-1",
        "M disjoint 1,2,3 4,5,6",
        "K zero 1 0.0 0.0 0",
        "K zero-ms 1 0.0 0.0 1",
        "K a-second 1 0.0 1.0 0",
        "K afternoon 120 77125.5 0.0 1",
        "K afternoon-later 120 77125.5 100.25 1",
        "K afternoon-later-whole 120 77125.5 100.25 0",
        "K midnight 1 86399.5 1.0 1",
        "K midnight-whole 1 86399.5 1.0 0",
        "K three-days 365 86399.0 259200.0 0",
        "K rounding-up 7 0.0 0.9996 1",
        "K rounding-half 7 0.0 1.0005 1",
        "K rounding-whole 7 0.0 59.9996 0",
        "K negative-elapsed 10 100.0 -50.5 1",
        "K day-zero 0 0.0 5.0 1",
        "K large-day 1000 12345.678 0.0 1",
        "K negative-day -3 5.0 0.0 0",
        "K an-hour 2 0.0 3661.0 0",
        "X one-stream 120 77125.5 2",
        "col 1 50434d3031 0 0.0,1.0,2.0 100.0,99.5,98.25",
        "col 2 50434d3031 0 0.0,1.0,2.0 0.0,0.0,3.0",
        "X two-streams-different-clocks 1 0.0 2",
        "col 1 41 0 0.0,1.0 1.0,2.0",
        "col 1 42 0 0.5,1.5 3.0,4.0",
        "X snr-and-lock 5 3600.0 2",
        "col 0 3430202d2043682041204c5f5243565231 0 0.0,1.0 -80.5,-79.25",
        "col 1 43682041 0 0.0,1.0 99.0,98.0",
        "X milliseconds-round-half-up 1 0.0 1",
        "col 1 41 0 0.0004,0.0005,0.0015,0.9995 1.0,2.0,3.0,4.0",
        "X ties-either-side-of-base 1 0.0 1",
        "col 1 41 0 -0.0625,0.0625,0.1875 1.0,2.0,3.0",
        "X nan-and-infinity 1 0.0 1",
        "col 1 41 0 0.0,1.0,2.0 nan,inf,-inf",
        "X last-in-a-millisecond-wins 1 0.0 1",
        "col 1 41 0 1.0001,1.0002,1.0003 1.0,2.0,3.0",
        "X out-of-order 1 0.0 1",
        "col 1 41 0 2.0,0.0,1.0 3.0,1.0,2.0",
        "X a-day-boundary 1 86398.0 1",
        "col 1 41 0 0.0,1.0,2.0,3.0 1.0,2.0,3.0,4.0",
        "X two-sources 1 0.0 3",
        "col 1 41 0 0.0,1.0 1.0,2.0",
        "col 1 41 1 0.0,1.0 3.0,4.0",
        "col 2 41 2 0.0 5.0",
        "X empty-and-non-ascii-names 1 0.0 2",
        "col 1 - 0 0.0 1.0",
        "col 1 52c3a9636570746f7220cea9 0 0.0 2.0",
        "X more-times-than-values 1 0.0 1",
        "col 1 41 0 0.0,1.0,2.0 1.0,2.0",
        "X an-empty-column 1 0.0 2",
        "col 1 41 0 0.0,1.0 1.0,2.0",
        "col 1 42 0 - -",
        "X six-decimals 1 0.0 1",
        "col 1 41 0 0.0,1.0 0.1234565,1234567.8901234",
    };

    /// <summary>What the core answered, one line per item (see the summary).</summary>
    public static readonly string[] Lines =
    {
        "C|all-three-kinds|1|1|0|1|0|3",
        "s|0|1|41|41|0|0|0|0|90|91|0,1|90,91",
        "s|1|2|41|41|0|0|0|0|0|1|0,1|0,1",
        "s|2|0|3430202d2043682041204c5f5243565232|43682041|2|0|40|0|-80.5|-79.5|0,1|-80.5,-79.5",
        "C|basic-lock|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|blank-first-line|0|0|0|0|0|0",
        "C|blank-lines|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|cell-number-forms|1|1|0|3|0|1",
        "s|0|1|41|41|0|0|0|0|0.10000000000000001|100|0,1,2,3|100,0.5,5,0.10000000000000001",
        "C|cell-overflow|1|1|0|2|0|1",
        "s|0|1|41|41|0|0|0|0|4.9406564584124654e-324|4.9406564584124654e-324|2|4.9406564584124654e-324",
        "C|cell-padding|1|1|0|3|0|1",
        "s|0|1|41|41|0|0|0|0|-7|8|0,1,2,3|5,6,-7,8",
        "C|cell-special-values|1|1|0|5|0|1",
        "s|0|1|41|41|0|0|0|0|inf|inf|0,1,2,3,5|nan,inf,-inf,nan,inf",
        "C|channel-index-counts|1|1|0|0|0|6",
        "s|0|0|3430202d2041204c5f5243565231|41|1|0|40|0|1|1|0|1",
        "s|1|0|3430202d204120525f5243565231|41|1|1|40|0|2|2|0|2",
        "s|2|0|3430202d204120435f5243565231|41|1|2|40|0|3|3|0|3",
        "s|3|0|3430202d2041204c5f5243565232|41|2|0|40|0|4|4|0|4",
        "s|4|0|3431202d2042204c5f5243565231|42|1|0|41|0|5|5|0|5",
        "s|5|0|3431202d204220525f5243565231|42|1|1|41|0|6|6|0|6",
        "C|crlf|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|duplicate-headers|1|1|0|0|0|2",
        "s|0|1|41|41|0|0|0|0|1|1|0|1",
        "s|1|1|41|41|0|0|0|0|2|2|0|2",
        "C|empty-cell|1|1|0|2|0|2",
        "s|0|1|41|41|0|0|0|0|1|5|0,2|1,5",
        "s|1|1|42|42|0|0|0|0|4|6|1,2|4,6",
        "C|empty-file|0|0|0|0|0|0",
        "C|empty-series-header|1|1|0|0|0|2",
        "s|0|0|-|-|0|0|0|0|1|1|0|1",
        "s|1|1|41|41|0|0|0|0|2|2|0|2",
        "C|every-cell-bad|0|1|0|0|0|0",
        "C|every-row-bad|0|0|0|0|2|0",
        "C|extra-fields|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|1|4|0,1|1,4",
        "C|first-good-row-sets-base|1|2|11045.25|1|1|1",
        "s|0|1|41|41|0|0|0|0|2|3|0,1|2,3",
        "C|first-row-bad-value|1|2|11045.25|1|0|1",
        "s|0|1|41|41|0|0|0|0|3|3|1|3",
        "C|first-row-short|1|2|11046.25|0|1|2",
        "s|0|1|41|41|0|0|0|0|2|2|0|2",
        "s|1|1|42|42|0|0|0|0|3|3|0|3",
        "C|header-case|0|0|0|0|0|0",
        "C|header-only|0|0|0|0|0|0",
        "C|invalid-utf8-name|1|1|0|0|0|1",
        "s|0|1|52e963657074657572|52e963657074657572|0|0|0|0|50|50|0|50",
        "C|lock-orders-after-snr|1|1|0|0|0|4",
        "s|0|1|5a6564|5a6564|0|0|0|0|1|1|0|1",
        "s|1|0|3430202d2041204c5f5243565231|41|1|0|40|0|2|2|0|2",
        "s|2|1|41|41|0|0|0|0|3|3|0|3",
        "s|3|2|5a6564|5a6564|0|0|0|0|4|4|0|4",
        "C|lone-cr|0|0|0|0|0|0",
        "C|mixed-line-ends|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|no-quoting|1|1|0|0|0|2",
        "s|0|0|2241|-|0|0|0|0|1.7976931348623157e+308|-1.7976931348623157e+308|-|-",
        "s|1|0|42204c6f636b2028252922|-|0|1|0|0|6|6|0|6",
        "C|no-trailing-newline|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|non-ascii-names|1|1|0|0|0|1",
        "s|0|1|52c3a9636570746f7220cea9|52c3a9636570746f7220cea9|0|0|0|0|50|50|0|50",
        "C|series-header-padded|1|1|0|0|0|1",
        "s|0|1|41|41|0|0|0|0|50|50|0|50",
        "C|short-row|1|1|0|2|1|2",
        "s|0|1|41|41|0|0|0|0|1|5|0,2|1,5",
        "s|1|1|42|42|0|0|0|0|2|6|0,2|2,6",
        "C|sources-keep-counts|1|1|0|0|0|4",
        "s|0|0|3430202d2041204c5f5243565231|41|1|0|40|0|1|1|0|1",
        "s|1|0|3430202d204120525f5243565231|41|1|1|40|0|2|2|0|2",
        "s|2|0|3430202d2041204c5f5243565231|41|1|0|40|1|3|3|0|3",
        "s|3|0|3430202d204120525f5243565231|41|1|1|40|1|4|4|0|4",
        "C|time-backwards|1|1|5|4|0|1",
        "s|0|1|41|41|0|0|0|0|1|3|0,-4,4|1,2,3",
        "C|time-column-alone|0|0|0|0|0|0",
        "C|time-empty-field|1|1|1|0|1|1",
        "s|0|1|41|41|0|0|0|0|2|2|0|2",
        "C|time-exponent|1|1|10|0|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,-9|1,2",
        "C|time-five-fields|1|1|1|0|1|1",
        "s|0|1|41|41|0|0|0|0|2|2|0|2",
        "C|time-fraction-day|1|1|1|0|1|1",
        "s|0|1|41|41|0|0|0|0|2|2|0|2",
        "C|time-header-padded|1|1|0|0|0|1",
        "s|0|1|41|41|0|0|0|0|50|50|0|50",
        "C|time-midnight|1|1|86399.5|0.75|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,0.75|1,2",
        "C|time-negative-day|1|-1|0|86400|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,86400|1,2",
        "C|time-out-of-range|1|1|93735.5|0|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,-93735.5|1,2",
        "C|time-repeated|1|1|1|0|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,0|1,2",
        "C|time-signed|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,1|1,2",
        "C|time-spaced|1|1|0|1.5|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,1.5|1,2",
        "C|time-three-fields|1|1|1|0|1|1",
        "s|0|1|41|41|0|0|0|0|2|2|0|2",
        "C|time-unpadded|1|1|0|1.5|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,1.5|1,2",
        "C|time-wide|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|1|2|0,1|1,2",
        "C|trailing-comma-header|1|1|0|0|0|2",
        "s|0|1|41|41|0|0|0|0|50|50|0|50",
        "s|1|0|-|-|0|0|0|0|7|7|0|7",
        "C|two-sources-one-label|1|1|0|0|0|3",
        "s|0|1|41|41|0|0|0|0|1|1|0|1",
        "s|1|1|41|41|0|0|0|2|2|2|0|2",
        "s|2|2|41|41|0|0|0|2|3|3|0|3",
        "C|unparseable-cell|1|1|0|1|0|2",
        "s|0|1|41|41|0|0|0|0|3|3|1|3",
        "s|1|1|42|42|0|0|0|0|2|4|0,1|2,4",
        "C|utf16be-bom|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|utf16le-bom|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|utf8-bom|1|1|0|1|0|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|whitespace-line|1|1|0|1|1|1",
        "s|0|1|41|41|0|0|0|0|50|60|0,1|50,60",
        "C|wrong-time-header|0|0|0|0|0|0",
        "P|another-section|-|-|0|0|0|0|0|0",
        "P|bits-and-words-too-small|-|-|0|0|0|0|0|0",
        "P|bits-as-text|-|-|1|800|0|0|0|0",
        "P|bits-at-minimum|-|-|1|64|0|0|0|0",
        "P|bits-below-minimum|-|-|0|0|0|0|0|0",
        "P|bits-fraction|-|-|0|0|0|0|0|0",
        "P|bits-large|-|-|1|70000|0|0|0|0",
        "P|bits-minimum-beats-words|-|-|1|64|0|0|0|0",
        "P|bits-negative|-|-|0|0|0|0|0|0",
        "P|bits-padded|-|-|1|800|0|0|0|0",
        "P|bits-plus|-|-|1|800|0|0|0|0",
        "P|bits-too-small-then-words|-|-|1|1024|1|0|0|0",
        "P|bits-zero|-|-|0|0|0|0|0|0",
        "P|both-flags-off|4645364232383430|4646464630303030|1|800|0|0|1|0",
        "P|complete|4645364232383430|4646464630303030|1|800|0|1|1|1",
        "P|crlf|4645364232383430|-|1|800|0|0|1|1",
        "P|duplicate-keys-last-wins|42424242|-|0|0|0|0|0|0",
        "P|empty|-|-|0|0|0|0|0|0",
        "P|empty-values|-|-|0|0|0|0|0|0",
        "P|inverted-empty|-|-|0|0|0|0|0|0",
        "P|inverted-false|-|-|0|0|0|0|0|0",
        "P|inverted-one|-|-|0|0|0|1|0|0",
        "P|inverted-quoted|-|-|0|0|0|1|0|0",
        "P|inverted-true|-|-|0|0|0|1|0|0",
        "P|inverted-uppercase|-|-|0|0|0|1|0|0",
        "P|inverted-yes|-|-|0|0|0|1|0|0",
        "P|inverted-zero|-|-|0|0|0|0|0|0",
        "P|key-case-matters|-|-|0|0|0|0|0|0",
        "P|lowercase-kept-as-written|6665366232383430|6666666630303030|0|0|0|0|0|0",
        "P|mask-only|-|4646303046463030|0|0|0|0|0|0",
        "P|no-section|-|-|0|0|0|0|0|0",
        "P|non-ascii-pattern|73747261c39f6520cebb|-|0|0|0|0|0|0",
        "P|not-hex-kept|7a7a2d31|78|0|0|0|0|0|0",
        "P|padded-value|202041422020|-|0|0|0|0|0|0",
        "P|pattern-only|3333344141424246|-|0|0|0|0|0|0",
        "P|randomized-absent|4142|-|0|0|0|1|0|0",
        "P|randomized-empty|-|-|0|0|0|0|1|0",
        "P|randomized-false|-|-|0|0|0|0|1|0",
        "P|randomized-one|-|-|0|0|0|0|1|1",
        "P|randomized-quoted-false|-|-|0|0|0|0|1|0",
        "P|randomized-true|-|-|0|0|0|0|1|1",
        "P|randomized-yes|-|-|0|0|0|0|1|1",
        "P|randomized-zero|-|-|0|0|0|0|1|0",
        "P|shipped-framesync_PRN11|4133343543413543|4646464646464646|1|2047|0|0|1|0",
        "P|shipped-framesync_PRN15|3333344141424246|4646464646464646|1|32767|0|0|1|0",
        "P|shipped-framesync_rcvr_default|4645364232383430|4646464646464646|1|800|0|0|1|0",
        "P|trailing-comment|4645364232383430|-|1|800|0|0|0|0",
        "P|unquoted-value|4645364232383430|-|0|0|0|0|0|0",
        "P|utf16be-bom|4645364232383430|-|1|800|0|0|0|0",
        "P|utf16le-bom|4645364232383430|-|1|800|0|0|1|1",
        "P|utf8-bom|4645364232383430|-|1|800|0|0|0|0",
        "P|valid-bits-beat-words|-|-|1|800|0|0|0|0",
        "P|words-1|-|-|0|0|0|0|0|0",
        "P|words-2|-|-|0|0|0|0|0|0",
        "P|words-4|-|-|1|64|1|0|0|0",
        "P|words-64|-|-|1|1024|1|0|0|0",
        "P|words-huge|-|-|1|1600000|1|0|0|0",
        "P|words-negative|-|-|0|0|0|0|0|0",
        "W|typical|1|5b4672616d655d0d0a4672616d6553796e63203d20224645364232383430220d0a4672616d6553796e634d61736b203d20224646464630303030220d0a426974735065724672616d65203d203830300d0a496e766572746564203d20747275650d0a52616e646f6d697a6564203d20747275650d0a",
        "W|flags-off|1|5b4672616d655d0d0a4672616d6553796e63203d20223333344141424246220d0a4672616d6553796e634d61736b203d2022220d0a426974735065724672616d65203d2033323736370d0a496e766572746564203d2066616c73650d0a52616e646f6d697a6564203d2066616c73650d0a",
        "W|empty-all|1|5b4672616d655d0d0a4672616d6553796e63203d2022220d0a4672616d6553796e634d61736b203d2022220d0a426974735065724672616d65203d20300d0a496e766572746564203d2066616c73650d0a52616e646f6d697a6564203d2066616c73650d0a",
        "W|quotes-and-backslash|1|5b4672616d655d0d0a4672616d6553796e63203d2022415c22425c5c43220d0a4672616d6553796e634d61736b203d20225c22220d0a426974735065724672616d65203d2036340d0a496e766572746564203d2066616c73650d0a52616e646f6d697a6564203d20747275650d0a",
        "W|non-ascii|1|5b4672616d655d0d0a4672616d6553796e63203d2022636166c3a920cea9220d0a4672616d6553796e634d61736b203d2022c3a9220d0a426974735065724672616d65203d20313032340d0a496e766572746564203d20747275650d0a52616e646f6d697a6564203d2066616c73650d0a",
        "W|embedded-newline|1|5b4672616d655d0d0a4672616d6553796e63203d202241420d0a4344220d0a4672616d6553796e634d61736b203d2022220d0a426974735065724672616d65203d2036340d0a496e766572746564203d2066616c73650d0a52616e646f6d697a6564203d2066616c73650d0a",
        "W|negative-bits|1|5b4672616d655d0d0a4672616d6553796e63203d20224142220d0a4672616d6553796e634d61736b203d2022220d0a426974735065724672616d65203d202d350d0a496e766572746564203d2066616c73650d0a52616e646f6d697a6564203d2066616c73650d0a",
        "W|maximum|1|5b4672616d655d0d0a4672616d6553796e63203d202246464646464646464646464646464646220d0a4672616d6553796e634d61736b203d202246464646464646464646464646464646220d0a426974735065724672616d65203d2036353533360d0a496e766572746564203d20747275650d0a52616e646f6d697a6564203d20747275650d0a",
        "M|same|1|-|-",
        "M|reordered|1|-|-",
        "M|missing-one|0|14|-",
        "M|extra-one|0|-|14",
        "M|missing-and-extra|0|13|15,16",
        "M|repeats|1|-|-",
        "M|both-empty|1|-|-",
        "M|empty-template|0|-|3,5",
        "M|empty-file|0|2,7|-",
        "M|unsorted-output|0|1,3,7,9|2,4,6,8",
        "M|negatives-and-extremes|0|-2147483648,2147483647|-",
        "M|disjoint|0|1,2,3|4,5,6",
        "K|zero|3030313a30303a30303a3030",
        "K|zero-ms|3030313a30303a30303a30302e303030",
        "K|a-second|3030313a30303a30303a3031",
        "K|afternoon|3132303a32313a32353a32352e353030",
        "K|afternoon-later|3132303a32313a32373a30352e373530",
        "K|afternoon-later-whole|3132303a32313a32373a3035",
        "K|midnight|3030323a30303a30303a30302e353030",
        "K|midnight-whole|3030323a30303a30303a3030",
        "K|three-days|3336383a32333a35393a3539",
        "K|rounding-up|3030373a30303a30303a30302e31303030",
        "K|rounding-half|3030373a30303a30303a30312e303030",
        "K|rounding-whole|3030373a30303a30303a3539",
        "K|negative-elapsed|3031303a30303a30303a34392e353030",
        "K|day-zero|3030303a30303a30303a30352e303030",
        "K|large-day|313030303a30333a32353a34352e363738",
        "K|negative-day|2d30333a30303a30303a3035",
        "K|an-hour|3030323a30313a30313a3031",
        "X|one-stream|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c50434d3031204c6f636b202825292c50434d303120416363756d756c61746564204d6973736564204672616d65730d0a3132303a32313a32353a32352e3530302c3130302e3030303030302c302e3030303030300d0a3132303a32313a32353a32362e3530302c39392e3530303030302c302e3030303030300d0a3132303a32313a32353a32372e3530302c39382e3235303030302c332e3030303030300d0a",
        "X|two-streams-different-clocks|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825292c42204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030302c0d0a3030313a30303a30303a30302e3530302c2c332e3030303030300d0a3030313a30303a30303a30312e3030302c322e3030303030302c0d0a3030313a30303a30303a30312e3530302c2c342e3030303030300d0a",
        "X|snr-and-lock|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c3430202d2043682041204c5f52435652312c43682041204c6f636b202825290d0a3030353a30313a30303a30302e3030302c2d38302e3530303030302c39392e3030303030300d0a3030353a30313a30303a30312e3030302c2d37392e3235303030302c39382e3030303030300d0a",
        "X|milliseconds-round-half-up|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030300d0a3030313a30303a30303a30302e3030312c322e3030303030300d0a3030313a30303a30303a30302e3030322c332e3030303030300d0a3030313a30303a30303a30312e3030302c342e3030303030300d0a",
        "X|ties-either-side-of-base|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030303a32333a35393a35392e3933372c312e3030303030300d0a3030313a30303a30303a30302e3036332c322e3030303030300d0a3030313a30303a30303a30302e3138382c332e3030303030300d0a",
        "X|nan-and-infinity|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30302e3030302c6e616e0d0a3030313a30303a30303a30312e3030302c696e660d0a3030313a30303a30303a30322e3030302c2d696e660d0a",
        "X|last-in-a-millisecond-wins|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30312e3030302c332e3030303030300d0a",
        "X|out-of-order|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030300d0a3030313a30303a30303a30312e3030302c322e3030303030300d0a3030313a30303a30303a30322e3030302c332e3030303030300d0a",
        "X|a-day-boundary|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a32333a35393a35382e3030302c312e3030303030300d0a3030313a32333a35393a35392e3030302c322e3030303030300d0a3030323a30303a30303a30302e3030302c332e3030303030300d0a3030323a30303a30303a30312e3030302c342e3030303030300d0a",
        "X|two-sources|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825292c53317c2041204c6f636b202825292c53327c204120416363756d756c61746564204d6973736564204672616d65730d0a3030313a30303a30303a30302e3030302c312e3030303030302c332e3030303030302c352e3030303030300d0a3030313a30303a30303a30312e3030302c322e3030303030302c342e3030303030302c0d0a",
        "X|empty-and-non-ascii-names|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c204c6f636b202825292c52c3a9636570746f7220cea9204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030302c322e3030303030300d0a",
        "X|more-times-than-values|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030300d0a3030313a30303a30303a30312e3030302c322e3030303030300d0a",
        "X|an-empty-column|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825292c42204c6f636b202825290d0a3030313a30303a30303a30302e3030302c312e3030303030302c0d0a3030313a30303a30303a30312e3030302c322e3030303030302c0d0a",
        "X|six-decimals|1|54696d652028444f593a48483a4d4d3a53532e6d6d6d292c41204c6f636b202825290d0a3030313a30303a30303a30302e3030302c302e3132333435360d0a3030313a30303a30303a30312e3030302c313233343536372e3839303132330d0a",
    };
}
