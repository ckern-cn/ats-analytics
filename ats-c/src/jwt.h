#ifndef ATS_JWT_H_INCLUDED
#define ATS_JWT_H_INCLUDED

/* Returns a kore_free()-able token, or NULL. */
char	*ats_jwt_sign(const char *user_name, long long iat, long long exp);

/*
 * 0 = valid token for the configured admin user, -1 = invalid.
 * Verifies the HS256 signature, alg, nbf/iat/exp and the user_name claim.
 */
int	ats_jwt_verify(const char *token);

#endif
