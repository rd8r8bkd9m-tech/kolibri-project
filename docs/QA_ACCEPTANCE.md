# Kolibri QA Acceptance

## Every iteration is incomplete until all four gates pass

1. docs updated
2. targeted backend tests green
3. frontend typecheck and build green
4. browser or product smoke green

## Frontend acceptance

- desktop: `1280`, `1536`
- mobile: `390`, `768`
- light and dark theme
- no overlap
- no horizontal overflow
- no dead buttons

### Required flows

- create/select/rename/pin/delete chat
- send/stream/stop
- edit and resend
- file/image preview
- voice open/close
- workspace open/close
- settings open/close
- login/logout/profile/preferences

## Backend acceptance

- auth status
- profile/preferences roundtrip
- conversation metadata CRUD
- chat answer path
- math exactness
- weather entity switching
- 5-turn follow-up stability

## Runtime acceptance

- ingest updates memory
- swarm status responds
- `.kpack` import/export works
- background learning state visible

## Release rule

No document or status report may claim completion if any gate above is red.
