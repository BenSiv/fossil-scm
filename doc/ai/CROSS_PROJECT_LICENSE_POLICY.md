# Cross-Project License Policy

This note is an engineering policy for work shared between this Fossil fork and
the companion Goose fork. It is not legal advice.

## Licenses In Play

- Fossil fork: Simplified BSD / 2-Clause BSD
- Goose fork: Apache-2.0

## Working Rules

1. Prefer interface sharing over source sharing.
   - Share API contracts, schemas, examples, fixtures, and architecture docs.
   - Reimplement behavior on each side from the shared contract.
2. Do not directly copy Apache-2.0 Goose runtime source files into Fossil.
3. Do not directly copy Fossil runtime source files into Goose unless:
   - the BSD notice is preserved
   - provenance is explicit
   - the file is clearly marked as imported or derived
4. If code must be shared, place it in a deliberately created layer with an
   explicit license policy rather than letting it drift between repos.
5. If third-party-derived code is intentionally imported, isolate it under a
   path such as:
   - `third_party/goose-derived/`
   - `third_party/fossil-derived/`

## Safe Shared Artifacts

These are the preferred shared outputs between the repos:

- HTTP endpoint contracts
- JSON envelope definitions
- SSE event schemas
- example payloads
- test vectors
- compatibility matrices
- architecture plans

## Practical Contributor Rule

When implementing the integration:

- copy ideas freely
- copy contracts deliberately
- do not copy source files casually

That keeps the two projects aligned technically without creating accidental
license ambiguity.
