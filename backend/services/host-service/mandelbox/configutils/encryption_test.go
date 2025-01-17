package configutils

import (
	"testing"
)

// TestHeaderExtraction tests the helper function that extracts
// the salt and nonce from the header of the encrypted data.
func TestHeaderExtraction(t *testing.T) {
	t.Run("File too short", func(t *testing.T) {
		file := []byte("short")
		_, _, _, err := getSaltNonceAndDataFromEncryptedFile(file, defaultSaltLength, aes256GCMNonceLength)
		if err == nil {
			t.Errorf("expected error, got nil")
		}
	})

	t.Run("No salt header provided", func(t *testing.T) {
		file := []byte("somesalt12345678Nonce_validnonce12filecontents")
		_, _, _, err := getSaltNonceAndDataFromEncryptedFile(file, defaultSaltLength, aes256GCMNonceLength)
		if err == nil {
			t.Errorf("expected error, got nil")
		}
	})

	t.Run("No nonce header provided", func(t *testing.T) {
		file := []byte("Salt_somesalt12345678filecontentsasdfasdfasdfas")
		_, _, _, err := getSaltNonceAndDataFromEncryptedFile(file, defaultSaltLength, aes256GCMNonceLength)
		if err == nil {
			t.Errorf("expected error, got nil")
		}
	})

	t.Run("Valid headers", func(t *testing.T) {
		expectedSalt := "somesalt12345678"
		expectedNonce := "validnonce12"
		expectedFileContents := "hello world"
		file := []byte(saltHeader + expectedSalt + nonceHeader + expectedNonce + expectedFileContents)

		salt, nonce, data, err := getSaltNonceAndDataFromEncryptedFile(file, defaultSaltLength, aes256GCMNonceLength)
		if err != nil {
			t.Errorf("unexpected error: %v", err)
		}

		if string(salt) != expectedSalt {
			t.Errorf("salt is not correct: got %s, want %s", salt, expectedSalt)
		}

		if string(nonce) != expectedNonce {
			t.Errorf("nonce is not correct: got %s, want %s", nonce, expectedNonce)
		}

		if string(data) != expectedFileContents {
			t.Errorf("data is not correct: got %s, want %s", data, expectedFileContents)
		}
	})
}

// TestEncryptionFormat tests that the encryption of a file results in the expected format.
func TestEncryptionFormat(t *testing.T) {
	plaintext := []byte("hello world")
	encryptedData, err := EncryptAES256GCM("password", plaintext)
	if err != nil {
		t.Errorf("unexpected error encrypting data: %v", err)
	}

	_, _, _, err = getSaltNonceAndDataFromEncryptedFile(encryptedData, defaultSaltLength, aes256GCMNonceLength)
	if err != nil {
		t.Errorf("unexpected error parsing headers: %v", err)
	}
}

// TestEncryptionDecryptionIntegration tests that we can successfully encrypt and decrypt a file.
func TestEncryptionDecryptionIntegration(t *testing.T) {
	t.Run("Valid file", func(t *testing.T) {
		plaintext := []byte("hello world")
		encryptedData, err := EncryptAES256GCM("password", plaintext)
		if err != nil {
			t.Errorf("unexpected error encrypting data: %v", err)
		}

		decryptedData, err := DecryptAES256GCM("password", encryptedData)
		if err != nil {
			t.Errorf("unexpected error decrypting data: %v", err)
		}

		if string(decryptedData) != string(plaintext) {
			t.Errorf("decrypted data is not correct: got %s, want %s", decryptedData, plaintext)
		}
	})

	t.Run("Invalid password", func(t *testing.T) {
		plaintext := []byte("hello world")
		encryptedData, err := EncryptAES256GCM("password", plaintext)
		if err != nil {
			t.Errorf("unexpected error encrypting data: %v", err)
		}

		_, err = DecryptAES256GCM("wrongpassword", encryptedData)
		if err == nil {
			t.Errorf("expected error decrypting data, got nil")
		}
	})

	t.Run("Corrupted data", func(t *testing.T) {
		plaintext := []byte("hello world")
		encryptedData, err := EncryptAES256GCM("password", plaintext)
		if err != nil {
			t.Errorf("unexpected error encrypting data: %v", err)
		}

		// Corrupt the data by truncating the last byte.
		encryptedData = encryptedData[:len(encryptedData)-1]

		_, err = DecryptAES256GCM("password", encryptedData)
		if err == nil {
			t.Errorf("expected error decrypting data, got nil")
		}
	})

	t.Run("Invalid headers", func(t *testing.T) {
		plaintext := []byte("hello world")
		encryptedData, err := EncryptAES256GCM("password", plaintext)
		if err != nil {
			t.Errorf("unexpected error encrypting data: %v", err)
		}

		// Corrupt the salt header by removing the first byte.
		encryptedData = encryptedData[1:]

		_, err = DecryptAES256GCM("password", encryptedData)
		if err == nil {
			t.Errorf("expected error decrypting data, got nil")
		}
	})
}
