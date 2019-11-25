from .imports import *

def genHaiku(n):
    haikunator = Haikunator()
    haikus = [haikunator.haikunate(delimiter='') + str(np.random.randint(0, 10000)) for _ in range(0, n)]
    haikus = [haiku[0: np.min([15, len(haiku)])] for haiku in haikus]
    return haikus

def generatePassword():
    upperCase = string.ascii_uppercase
    lowerCase = string.ascii_lowercase
    specialChars = '!@#$%^*'
    numbers = '1234567890'
    c1 = ''.join([random.choice(upperCase) for _ in range(0,3)])
    c2 = ''.join([random.choice(lowerCase) for _ in range(0,9)]) + c1
    c3 = ''.join([random.choice(lowerCase) for _ in range(0,5)]) + c2
    c4 = ''.join([random.choice(numbers) for _ in range(0,4)]) + c3
    return ''.join(random.sample(c4,len(c4)))
